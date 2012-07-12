/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010-2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 */

#define G_LOG_DOMAIN "lgi"

#include <lua.h>
#include <lauxlib.h>

/* Lua 5.2 compatibility stuff. */
#if LUA_VERSION_NUM < 502
#define lua_absindex(L, i)						\
  ((i > 0 || i <= LUA_REGISTRYINDEX) ? i : lua_gettop (L) + i + 1)
#define lua_getuservalue(L, p) lua_getfenv (L, p)
#define lua_setuservalue(L, p) lua_setfenv (L, p)
#define luaL_setfuncs(L, regs, nup) luaL_register (L, NULL, regs)
#define luaL_len(L, p) lua_objlen (L, p)
#define lua_rawlen(L, p) lua_objlen (L, p)
void *luaL_testudata (lua_State *L, int arg, const char *name);
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

/* Creates cache table (optionally with given table __mode), stores it
   into registry to specified userdata address. */
void
lgi_cache_create (lua_State *L, gpointer key, const char *mode);

/* Initialization of modules. */
void lgi_ctype_init (lua_State *L);
void lgi_aggr_init (lua_State *L);
void lgi_compound_init (lua_State *L);
void lgi_call_init (lua_State *L);
void lgi_buffer_init (lua_State *L);

/* Metatable name of userdata for 'bytes' extension; see
   http://permalink.gmane.org/gmane.comp.lang.lua.general/79288 */
#define LGI_BYTES_BUFFER "bytes.bytearray"

/* Retrieve synchronization state, which can be used for entering and
   leaving the state using lgi_state_enter() and lgi_state_leave(). */
gpointer lgi_state_get_lock (lua_State *L);

/* Enters/leaves Lua state. */
void lgi_state_enter (gpointer left_state);
void lgi_state_leave (gpointer state_lock);

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
