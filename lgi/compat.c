/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011, 2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Compatibility layer for old Lua version.
 */

#include <string.h>
#include "lgi.h"


#if !defined LUA_VERSION_NUM || LUA_VERSION_NUM==501

/* Adapted from
   http://lua-users.org/wiki/CompatibilityWithLuaFive */
void
luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup)
{
  int i;
  luaL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {
    for (i = 0; i < nup; i++)
      lua_pushvalue(L, -nup);
    lua_pushstring(L, l->name);
    lua_pushcclosure(L, l->func, nup);
    lua_settable(L, -(nup + 3));
  }
  lua_pop(L, nup);
}

#endif
