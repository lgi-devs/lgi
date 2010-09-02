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

/* Global context of main thread, usable for callbacks to work with. */
extern lua_State* lgi_main_thread_state;

/* Reports specified GLib error as function return.  Returns number of items
 * pushed to the stack.  err instance is automatically freed. */
int lgi_error(lua_State* L, GError* err);

/* Puts parts of the name to the stack, to be concatenated by lua_concat.
   Returns number of pushed elements. */
int lgi_type_get_name(lua_State* L, GIBaseInfo* info);

/* Key in registry, containing table with all our private data. */
extern int lgi_regkey;
typedef enum lgi_reg
{
  /* Cache of created userdata objects, __mode=v */
  LGI_REG_CACHE = 1,

  /* compound.ref_repo -> repo type table. */
  LGI_REG_TYPEINFO = 2,

  /* whole repository, filled in by bootstrap. */
  LGI_REG_REPO = 3,

  LGI_REG__LAST
} LgiRegType;

/* Lua LgiCallable string name identification. */
#define LGI_CALLABLE "lgi.callable"
extern const struct luaL_reg lgi_callable_reg[];

#define LGI_CLOSUREGUARD "lgi.closureguard"
extern const struct luaL_reg lgi_closureguard_reg[];

/* Marshalls single value from Lua to GLib/C. Returns number of temporary
   entries pushed to Lua stack, which should be popped before function call
   returns. */
int lgi_marshal_2c(lua_State* L, GITypeInfo* ti, GIArgInfo* ai,
                   GITransfer xfer,  GIArgument* val, int narg,
                   GICallableInfo* ci, GIArgument* args);

/* Marshalls single value from GLib/C to Lua.  Returns TRUE if
   something was pushed to the stack. */
gboolean lgi_marshal_2lua(lua_State* L, GITypeInfo* ti, GIArgument* val, 
			  GITransfer xfer,
                          GICallableInfo* ci, GIArgument* args);

/* Parses given GICallableInfo, creates new userdata for it and stores
   it to the stack. Uses cache, so already parsed callable held in the
   cache is reused if possible. */
int lgi_callable_create(lua_State* L, GICallableInfo* ci);

/* Calls specified callable and arguments on the stack, using passed function
   address.  If it is NULL, an address is attempted to get from the info (if it
   is actually IFunctionInfo). func is stack index of callable object and args
   is stack index of first argument. */
int lgi_callable_call(lua_State* L, gpointer addr, int func, int args);

/* Creates closure for specified Lua function (or callable table or
   userdata). Returns user_data field for the closure and fills call_addr with
   executable address for the closure. */
gpointer lgi_closure_create(lua_State* L, GICallableInfo* ci, int target,
                            gboolean autodestroy, gpointer* call_addr);

/* GDestroyNotify-compatible callback for destroying closure. */
void lgi_closure_destroy(gpointer user_data);

/* Creates closure guard Lua userdata object and puts it on the stack.  Closure
 * guard automatically destroys the closure in its __gc metamethod. */
void lgi_closure_guard(lua_State* L, gpointer user_data);

/* Creates new compound of given address and type, pushes its userdata on the
 * lua stack. */
gboolean lgi_compound_create(lua_State* L, GIBaseInfo* ii, gpointer addr,
			     gboolean own);

/* Creates new struct including allocated place for it. */
int lgi_compound_create_struct(lua_State* L, GIBaseInfo* ii, gpointer* addr);

/* Retrieves compound-type parameter from given Lua-stack position, checks,
   whether it is suitable for requested ii type.  Returns pointer to the
   compound object, returns NULL if Lua-stack value is nil and optional is
   TRUE. */
gpointer lgi_compound_get(lua_State* L, int arg, GIBaseInfo* ii,
                          gboolean optional);
