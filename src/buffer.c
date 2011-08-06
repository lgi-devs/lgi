/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implementation of writable buffer object.
 */

#include <string.h>
#include "lgi.h"

static int
buffer_len (lua_State *L)
{
  luaL_checkudata (L, 1, LGI_BYTES_BUFFER);
  lua_pushnumber (L, lua_objlen (L, 1));
  return 1;
}

static int
buffer_tostring (lua_State *L)
{
  gpointer data = luaL_checkudata (L, 1, LGI_BYTES_BUFFER);
  lua_pushlstring (L, data, lua_objlen (L, 1));
  return 1;
}

static int
buffer_index (lua_State *L)
{
  int index;
  unsigned char *buffer = luaL_checkudata (L, 1, LGI_BYTES_BUFFER);
  index = lua_tonumber (L, 2);
  if (index > 0 && index <= lua_objlen (L, 1))
    lua_pushnumber (L, buffer[index - 1]);
  else
    {
      luaL_argcheck (L, !lua_isnoneornil (L, 2), 2, "nil index");
      lua_pushnil (L);
    }
  return 1;
}

static int
buffer_newindex (lua_State *L)
{
  int index;
  unsigned char *buffer = luaL_checkudata (L, 1, LGI_BYTES_BUFFER);
  index = luaL_checkint (L, 2);
  luaL_argcheck (L, index > 0 && index <= lua_objlen (L, 1), 2, "bad index");
  buffer[index - 1] = luaL_checkint (L, 3) & 0xff;
  return 0;
}

static const luaL_Reg buffer_mt_reg[] = {
  { "__len", buffer_len },
  { "__tostring", buffer_tostring },
  { "__index", buffer_index },
  { "__newindex", buffer_newindex },
  { NULL, NULL }
};

static int
buffer_new (lua_State *L)
{
  size_t size;
  gpointer *buffer;
  const char *source = NULL;

  if (lua_type (L, 1) == LUA_TSTRING)
    source = lua_tolstring (L, 1, &size);
  else
    size = luaL_checknumber (L, 1);
  buffer = lua_newuserdata (L, size);
  if (source)
    memcpy (buffer, source, size);
  else
    memset (buffer, 0, size);
  luaL_getmetatable (L, LGI_BYTES_BUFFER);
  lua_setmetatable (L, -2);
  return 1;
}

static int
ref_len (lua_State *L)
{
  LgiRef *ref = luaL_checkudata (L, 1, LGI_BYTES_REF);
  lua_pushnumber (L, ref->length);
  return 1;
}

static int
ref_tostring (lua_State *L)
{
  LgiRef *ref = luaL_checkudata (L, 1, LGI_BYTES_REF);
  lua_pushlstring (L, (const char *) ref->data, ref->length);
  return 1;
}

static int
ref_index (lua_State *L)
{
  int index;
  LgiRef *ref = luaL_checkudata (L, 1, LGI_BYTES_REF);
  index = lua_tonumber (L, 2);
  if (index > 0 && index <= ref->length)
    lua_pushnumber (L, ref->data[index - 1]);
  else
    {
      luaL_argcheck (L, !lua_isnoneornil (L, 2), 2, "nil index");
      lua_pushnil (L);
    }
  return 1;
}

static const luaL_Reg ref_mt_reg[] = {
  { "__len", ref_len },
  { "__tostring", ref_tostring },
  { "__index", ref_index },
  { NULL, NULL }
};

static int
ref_resize (lua_State *L)
{
  LgiRef *ref = luaL_checkudata (L, 1, LGI_BYTES_REF);
  ref->length = luaL_checkint (L, 2);
  return 0;
}

static const luaL_Reg buffer_reg[] = {
  { "new", buffer_new },
  { "resize", ref_resize },
  { NULL, NULL }
};

void
lgi_buffer_init (lua_State *L)
{
  /* Register metatables. */
  luaL_newmetatable (L, LGI_BYTES_BUFFER);
  luaL_register (L, NULL, buffer_mt_reg);
  luaL_newmetatable (L, LGI_BYTES_REF);
  luaL_register (L, NULL, ref_mt_reg);
  lua_pop (L, 2);

  /* Register global API. */
  lua_newtable (L);
  luaL_register (L, NULL, buffer_reg);
  lua_setfield (L, -2, "bytes");
}
