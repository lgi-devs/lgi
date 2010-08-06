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

/* Parses given GICallableInfo, creates new userdata for it and stores it to
   the stack. */
int lgi_callable_store(lua_State* L, GICallableInfo* ci);

/* Calls specified callable and arguments on the stack, using passed function
   address.  If it is NULL, an address is attempted to get from the info (if it
   is actually IFunctionInfo). func is stack index of callable object and args
   is stack index of first argument. */
int lgi_callable_call(lua_State* L, gpointer addr, int func, int args);
