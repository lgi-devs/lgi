/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010,2011,2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 */

#define G_LOG_DOMAIN "Lgi"

#include <lua.h>
#include <lauxlib.h>

/* Lua 5.2 compatibility stuff. */
#if LUA_VERSION_NUM >= 502
#define luaL_register(L, null, regs) luaL_setfuncs (L, regs, 0)
#define lua_equal(L, p1, p2) lua_compare (L, p1, p2, LUA_OPEQ)
#define lua_objlen(L, p) lua_rawlen (L, p)
#define lua_setfenv(L, p) lua_setuservalue (L, p)
#define lua_getfenv(L, p) lua_getuservalue (L, p)
#else
#define lua_absindex(L, i)						\
  ((i > 0 || i <= LUA_REGISTRYINDEX) ? i : lua_gettop (L) + i + 1)
#define lua_getuservalue(L, p) lua_getfenv (L, p)
#define lua_setuservalue(L, p) lua_setfenv (L, p)
#define luaL_setfuncs(L, regs) luaL_register (L, NULL, regs)
#define luaL_len(L, p) lua_objlen (L, p)
#define luaL_testudata(L, p, n) lgi_udata_test (L, p, n)
void lua_rawsetp (lua_State *L, int index, void *p);
void lua_rawgetp (lua_State *L, int index, void *p);
#endif
void *luaL_testudatap (lua_State *L, int arg, void *p);
void *luaL_checkudatap (lua_State *L, int arg, void *p);

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <girepository.h>
#include <gmodule.h>

/* Makes sure that Lua stack offset is absolute one, not relative. */
#define lgi_makeabs(L, x) do { if (x < 0) x += lua_gettop (L) + 1; } while (0)

/* Puts parts of the name to the stack, to be concatenated by lua_concat.
   Returns number of pushed elements. */
int lgi_type_get_name (lua_State *L, GIBaseInfo *info);

/* Stores repo type table associated with specified gtype (or BaseInfo
   if gtype is invalid). to the stack, or nil if no such table can be
   found in the repo. */
void lgi_type_get_repotype (lua_State *L, GType gtype, GIBaseInfo *info);

/* Gets GType from Lua index narg.  Accepts number and when it is
   other type, invokes Lua helper to convert. */
GType lgi_type_get_gtype (lua_State *L, int narg);

/* Allocates guard, a pointer-size userdata with associated destroy
   handler. Returns pointer to user_data stored inside guard. */
gpointer *lgi_guard_create (lua_State *L, GDestroyNotify destroy);

/* Creates cache table (optionally with given table __mode), stores it
   into registry to specified userdata address. */
void
lgi_cache_create (lua_State *L, gpointer key, const char *mode);

/* Initialization of modules. */
void lgi_marshal_init (lua_State *L);
void lgi_ctype_init (lua_State *L);
void lgi_record_init (lua_State *L);
void lgi_aggr_init (lua_State *L);
void lgi_compound_init (lua_State *L);
void lgi_call_init (lua_State *L);
void lgi_object_init (lua_State *L);
void lgi_callable_init (lua_State *L);
void lgi_gi_init (lua_State *L);
void lgi_buffer_init (lua_State *L);

/* Checks whether given argument is of specified udata - similar to
   luaL_testudata, which is missing in Lua 5.1 */
void *
lgi_udata_test (lua_State *L, int narg, const char *name);

/* Metatable name of userdata for 'bytes' extension; see
   http://permalink.gmane.org/gmane.comp.lang.lua.general/79288 */
#define LGI_BYTES_BUFFER "bytes.bytearray"

/* Metatable name of userdata - gi wrapped 'GIBaseInfo*' */
#define LGI_GI_INFO "lgi.gi.info"

/* Creates new instance of info from given GIBaseInfo pointer. */
int lgi_gi_info_new (lua_State *L, GIBaseInfo *info);

/* Assumes that 'typetable' can hold field 'name' which contains
   wrapped LGI_GI_INFO of function.  Returns address of this function,
   NULL if table does not contain such field. */
gpointer lgi_gi_load_function(lua_State *L, int typetable, const char *name);

/* Retrieve synchronization state, which can be used for entering and
   leaving the state using lgi_state_enter() and lgi_state_leave(). */
gpointer lgi_state_get_lock (lua_State *L);

/* Enters/leaves Lua state. */
void lgi_state_enter (gpointer left_state);
void lgi_state_leave (gpointer state_lock);

/* Special value for 'parent' argument of marshal_2c/lua.  When parent
   is set to this value, marshalling takes place always into pointer
   on the C side.  This isuseful when marshalling value from/to lists,
   arrays and hashtables. */
#define LGI_PARENT_FORCE_POINTER G_MAXINT

/* Another special value for 'parent' argument, meaning that the value
   should be handled as return value, according to ffi_call retval
   requirements. */
#define LGI_PARENT_IS_RETVAL (G_MAXINT - 1)

/* Marshalls single value from Lua to GLib/C. Returns number of temporary
   entries pushed to Lua stack, which should be popped before function call
   returns. */
int lgi_marshal_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
		    GITransfer xfer,  gpointer target, int narg,
		    int parent, GICallableInfo *ci, void **args);

/* If given parameter is out:caller-allocates, tries to perform
   special 2c marshalling.  If not needed, returns FALSE, otherwise
   stores single value with value prepared to be returned to C. */
gboolean lgi_marshal_2c_caller_alloc (lua_State *L, GITypeInfo *ti,
				      GIArgument *target, int pos);

/* Marshalls single value from GLib/C to Lua. If parent is non-0, it
   is stack index of parent structure/array in which this C value
   resides. */
void lgi_marshal_2lua (lua_State *L, GITypeInfo *ti, GITransfer xfer,
		       gpointer source, int parent,
		       GICallableInfo *ci, void **args);

/* Marshalls field to/from given memory (struct, union or
   object). Returns number of results pushed to the stack (0 or 1). */
int lgi_marshal_field (lua_State *L, gpointer object, gboolean getmode,
		       int parent_arg, int field_arg, int val_arg);

/* Implementation of object/record _access invocation. */
int lgi_marshal_access (lua_State *L, gboolean getmode,
			int compound_arg, int element_arg, int val_arg);

/* Parses given GICallableInfo, creates new userdata for it and stores
   it to the stack. Uses cache, so already parsed callable held in the
   cache is reused if possible. */
int lgi_callable_create (lua_State *L, GICallableInfo *ci, gpointer addr);

/* Creates container block for allocated closures.  Returns address of
   the block, suitable as user_data parameter. */
gpointer lgi_closure_allocate (lua_State *L, int count);

/* Allocates n-th closure in the closure block for specified Lua
   function (or callable table or userdata). Returns executable
   address for the closure. */
gpointer lgi_closure_create (lua_State* L, gpointer user_data,
			     GICallableInfo* ci, int target,
			     gboolean autodestroy);

/* GDestroyNotify-compatible callback for destroying closure. */
void lgi_closure_destroy (gpointer user_data);

/* Allocates and creates new record instance. Assumes that repotype table
   is on the stack, replaces it with newly created proxy. */
gpointer lgi_record_new (lua_State *L, int count);

/* Creates Lua-side part of given record. Assumes that repotype table
   is on the stack, replaces it with newly created proxy. If parent
   not zero, it is stack index of record parent (i.e. record of which
   the arg record is part of). */
void lgi_record_2lua (lua_State *L, gpointer addr, gboolean own, int parent);

/* Gets pointer to C-structure from given Lua-side object. Expects
   repo typetable of expected argument pushed on the top of the stack,
   removes it. */
gpointer lgi_record_2c (lua_State *L, int narg, gboolean optional,
			gboolean nothrow);

/* Creates Lua-side part (proxy) of given object. If the object is not
   owned (own == FALSE), an ownership is automatically acquired.  Returns
   number of elements pushed to the stack, i.e. always 1. */
int
lgi_object_2lua (lua_State *L, gpointer obj, gboolean own);

/* Gets pointer to C-side object represented by given Lua proxy. If
   gtype is not G_TYPE_INVALID, the real type is checked to conform to
   requested type. */
gpointer
lgi_object_2c (lua_State *L, int narg, GType gtype, gboolean optional,
	       gboolean nothrow, gboolean transfer);

/* Common datatype for aggregate (arrays and compounds). */
typedef struct _LgiAggregate
{
  /* Address of the aggregate in memory. */
  gpointer addr;

  /* Flag indicating whether aggregate is owned by this Lua proxy. */
  guint owned : 1;

  /* Flag indicating whether data for this aggregate are stored
     'inline'. */
  guint is_inline : 1;

  /* Index of the typeinfo index of the child element (used for
     arrays). */
  guint ntipos : 6;

  /* Number of items (used for arrays). */
  guint n_items : 24;
} LgiAggregate;

/* Returns aggregate's data area. Optionally checks whether it
   conforms to specified mt pointer in registry, returns NULL if
   not. */
LgiAggregate *
lgi_aggr_get (lua_State *L, int narg, gpointer mt);

/* Tries to pick up aggregate from aggregate cache, puts it on the
   stack and returns pointer to it.  If not found, returns NULL and
   stores nothing to the stack. */
LgiAggregate *
lgi_aggr_find (lua_State *L, gpointer addr, int parent);

/* Creates new aggregate, stores it into parent/cache tables.  If addr
   is NULL, creates 'inline' aggregate with 'size' reserved data area.
   Assigns metatable identified by mt pointer in registry. */
LgiAggregate *
lgi_aggr_create (lua_State *L, gpointer mt,
		 gpointer addr, int size, int parent);
/* Changes ownership information of the compound to specified value.
   'action' 1(TRUE) means that ownership is added to compound,
   0(FALSE) means that no change should occur, and -1 means that
   ownership should be removed.  If it results in non-owned, attempts
   to re-own the compound using '_ref' control function. */
gboolean
lgi_compound_own (lua_State *L, int narg, int action);

/* Gets Lua compound object for given record/union/object.  'addr' is
   address of the object or NULL if new object should be allocated.
   'owned' says whether addr already has ownership to keep with proxy,
   and is actually the same triple-state as 'action' for
   lgi_compound_own() is.  'parent' is optional index of object to
   keep alive while the record is alive. */
void
lgi_compound_2lua (lua_State *L, int ntypetable, gpointer addr, int owned,
		   int parent);

/* Returns C pointer to record from Lua proxy.  Returns NULL if narg
   is not compound, optionally conforming to specified ntype
   typetable. */
gpointer
lgi_compound_2c (lua_State *L, int narg, int ntype);

struct _LgiCTypeGuard;
typedef struct _LgiCTypeGuard LgiCTypeGuard;

/* Create CType guard, which is used to protect temporary values
   during marshaling. */
LgiCTypeGuard *
lgi_ctype_guard_create (lua_State *L, int n_items);

/* Commits all commitable items accumulated in the guard,
   i.e. deactivates destroy notification for them. */
void
lgi_ctype_guard_commit (lua_State *L, LgiCTypeGuard *guard);

/* Queries size and alignment of given type, advances *ntipos after
   the type definition. */
void
lgi_ctype_query (lua_State *L, int nti, int *ntipos,
		 gsize *size, gsize *align);

/* Converts value from 'narg' stack position to Lua value which is
   stored on the stack.  Type information is from table 'nti',
   starting at position 'ntipos'. */
void
lgi_ctype_2c (lua_State *L, LgiCTypeGuard *guard, int nti, int *ntipos,
	      int dir, int narg, gpointer target);

/* Converts value from C to Lua value and stores it on the stack.
   Type information is from table 'nti', starting at position
   'ntipos'. */
void
lgi_ctype_2lua (lua_State *L, LgiCTypeGuard *guard, int nti, int *ntipos,
		int dir, int parent, gpointer source);

#if !GLIB_CHECK_VERSION(2, 30, 0)
/* Workaround for broken g_struct_info_get_size() for GValue, see
   https://bugzilla.gnome.org/show_bug.cgi?id=657040 */
gsize lgi_struct_info_get_size (GIStructInfo *info);
#define g_struct_info_get_size lgi_struct_info_get_size
int lgi_field_info_get_offset (GIFieldInfo *info);
#define g_field_info_get_offset lgi_field_info_get_offset
#endif

/* Workaround method for broken g_object_info_get_*_function_pointer()
   in GI 1.32.0. (see https://bugzilla.gnome.org/show_bug.cgi?id=673282) */
gpointer lgi_object_get_function_ptr (GIObjectInfo *info,
				      const gchar *(*getter)(GIObjectInfo *));
