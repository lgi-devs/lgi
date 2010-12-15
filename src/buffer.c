/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implementation of writable buffer object.
 */

#include <string.h>
#include "lgi.h"

/* Lightuserdata address of this is registry index of buffer metatable. */
static int buffer_mt;

/* Structure with buffer data. */
typedef struct _Buffer
{
  size_t size;
  char data[1];
} Buffer;

gpointer
lgi_buffer_check (lua_State *L, int narg, size_t *size)
{
  gpointer data = NULL;
  luaL_checkstack (L, 2, "");
  if (lua_getmetatable (L, narg))
    {
      int equal;
      lua_pushlightuserdata (L, &buffer_mt);
      lua_rawget (L, LUA_REGISTRYINDEX);
      equal = lua_rawequal (L, -1, -2);
      lua_pop (L, 2);
      if (equal)
	{
	  Buffer *buffer = lua_touserdata (L, narg);
	  data = buffer->data;
	  if (size)
	    *size = buffer->size;
	}
    }
  return data;
}

static gpointer
buffer_get (lua_State *L, int narg, size_t *size)
{
  gpointer data = lgi_buffer_check (L, narg, size);
  if (!data)
    {
      lua_pushfstring (L, "expected buffer, got %s",
		       lua_typename (L, lua_type (L, narg)));
      luaL_argerror (L, narg, lua_tostring (L, -1));
    }
  return data;
}

static int
buffer_len (lua_State *L)
{
  size_t size;
  buffer_get (L, 1, &size);
  lua_pushnumber (L, size);
  return 1;
}

static int
buffer_tostring (lua_State *L)
{
  size_t size;
  gpointer data = buffer_get (L, 1, &size);
  lua_pushlstring (L, data, size);
  return 1;
}

static const luaL_Reg buffer_mt_reg[] = {
  { "__len", buffer_len },
  { "__tostring", buffer_tostring },
  { NULL, NULL }
};

static int
buffer_new (lua_State *L)
{
  size_t size;
  Buffer *buffer;
  const char *source = NULL;

  if (lua_type (L, 1) == LUA_TSTRING)
    source = lua_tolstring (L, 1, &size);
  else
    size = luaL_checknumber (L, 1);
  buffer = lua_newuserdata (L, G_STRUCT_OFFSET (Buffer, data) + size);
  buffer->size = size;
  if (source)
    memcpy (buffer->data, source, size);
  else
    memset (buffer->data, 0, size);
  lua_pushlightuserdata (L, &buffer_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);
  return 1;
}

void
lgi_buffer_init (lua_State *L)
{
  /* Register buffer metatable. */
  lua_pushlightuserdata (L, &buffer_mt);
  lua_newtable (L);
  luaL_register (L, NULL, buffer_mt_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Register global API. */
  lua_pushcfunction (L, buffer_new);
  lua_setfield (L, -2, "buffer_new");
}
