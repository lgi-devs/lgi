/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#define G_LOG_DOMAIN "Lgi"

#include <lua.h>
#include <lauxlib.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <girepository.h>

/* Lua stack dump for debugging purposes. */
#ifndef NDEBUG
const char *lgi_sd (lua_State* L);
#endif

/* Makes sure that Lua stack offset is absolute one, not relative. */
#define lgi_makeabs(L, x) do { if (x < 0) x += lua_gettop (L) + 1; } while (0)

/* Puts parts of the name to the stack, to be concatenated by lua_concat.
   Returns number of pushed elements. */
int lgi_type_get_name (lua_State *L, GIBaseInfo *info);

/* Stores repo type table associated with specified gtype (or BaseInfo
   if gtype is invalid). to the stack, or nil if no such table can be
   found in the repo. */
GType lgi_type_get_repotype (lua_State *L, GType gtype, GIBaseInfo *info);

/* Gets GType from Lua index narg.  Accepts number and when it is
   other type, invokes Lua helper to convert. */
GType lgi_type_get_gtype (lua_State *L, int narg);

/* Allocates guard, a pointer-size userdata with associated destroy
   handler. Returns pointer to user_data stored inside guard. */
gpointer *lgi_guard_create (lua_State *L, GDestroyNotify destroy);

/* lightuserdata of this address is a key in LUA_REGISTRYINDEX table
   to global repo table. */
extern int lgi_addr_repo;

/* Creates cache table (optionally with given table __mode), stores it into
   registry and returns ref to it. */
void
lgi_cache_create (lua_State *L, gpointer key, const char *mode);

/* Initialization of modules. */
void lgi_record_init (lua_State *L);
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
/* Returns address and possibly size of buffer object at given
   argument. Returns NULL if it is not of type buffer. */
gpointer lgi_buffer_check (lua_State *L, int narg, size_t *size);

/* Metatable name of userdata - gi wrapped 'GIBaseInfo*' */
#define LGI_GI_INFO "lgi.gi.info"

/* Creates new instance of info from given GIBaseInfo pointer. */
int lgi_gi_info_new (lua_State *L, GIBaseInfo *info);

/* Checks if narg is gi.info and if yes, returns it, otherwise returns
   NULL. */
GIBaseInfo *lgi_gi_info_test (lua_State *L, int narg);

/* Gets gtype of the type represented by typeinfo. */
GType lgi_get_gtype (lua_State *L, GITypeInfo *ti);

/* Marshalls single value from Lua to GLib/C. Returns number of temporary
   entries pushed to Lua stack, which should be popped before function call
   returns. */
int lgi_marshal_arg_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
			GITransfer xfer,  GIArgument *val, int narg,
			gboolean in_parent, gboolean use_pointer,
			GICallableInfo *ci, void **args);

/* Marshalls single value from Lua to GValue. ti is optional. */
void lgi_marshal_val_2c (lua_State *L, GITypeInfo *ti, GITransfer xfer,
			 GValue *val, int narg);

/* If given parameter is out;caller-allocates, tries to perform
   special 2c marshalling.  If not needed, returns FALSE, otherwise
   stores single value with value prepared to be returned to C. */
gboolean lgi_marshal_arg_2c_caller_alloc (lua_State *L, GITypeInfo *ti,
					  GIArgument *val, int pos);

/* Marshalls single value from GLib/C to Lua. If parent is non-0, it
   is stack index of parent structure/array in which this C value
   resides. */
void lgi_marshal_arg_2lua (lua_State *L, GITypeInfo *ti, GITransfer xfer,
			   GIArgument *val, int parent, gboolean use_pointer,
			   GICallableInfo *ci, void **args);

/* Marshalls single value from GValue to Lua. ti is optional. */
void lgi_marshal_val_2lua (lua_State *L, GITypeInfo *ti, GITransfer xfer,
			   const GValue *val);

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

/* Creates closure for specified Lua function (or callable table or
   userdata). Returns user_data field for the closure and fills call_addr with
   executable address for the closure. */
gpointer lgi_closure_create (lua_State* L, GICallableInfo* ci, int target,
			     gboolean autodestroy, gpointer* call_addr);

/* GDestroyNotify-compatible callback for destroying closure. */
void lgi_closure_destroy (gpointer user_data);

/* Creates GClosure which invokes specified target. */
GClosure *lgi_gclosure_create (lua_State *L, int target);

/* Allocates and creates new record instance. */
gpointer lgi_record_new (lua_State *L, GIBaseInfo *ri);

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
   owned (own == FALSE), an ownership is automatically acquired. */
void
lgi_object_2lua (lua_State *L, gpointer obj, gboolean own);

/* Gets pointer to C-side object represented by given Lua proxy. If
   gtype is not G_TYPE_INVALID, the real type is checked to conform to
   requested type. */
gpointer
lgi_object_2c (lua_State *L, int narg, GType gtype, gboolean optional,
	       gboolean nothrow);
