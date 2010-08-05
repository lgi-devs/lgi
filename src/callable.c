/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#include "lgi.h"

#include <ffi.h>

/* Structure representing userdata allocated for any callable, i.e. function,
   method, signal, vtable, callback... */
typedef struct
{
  /* Stored callable info. */
  GICallableInfo* info;

  /* Address of the function, if target is IFunctionInfo. */
  gpointer address;

  /* Flags with function characteristics. */
  guint has_self : 1;
  guint throws : 1;
  guint argc : 6;

  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* '1 (retval) + argc + has_self + throws' ffi_type slots follow. */
  ffi_type ffi_args[1];
} LgiCallable;

int
lgi_callable_store(lua_State* L, GICallableInfo* info)
{
  LgiCallable* callable;
  gint nargs;
  const gchar* symbol;
  
  /* Check cache, whether this callable object is already present. */
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_CACHE);
  lua_pushstring(L, g_base_info_get_namespace(info));
  lua_pushstring(L, ".");
  lua_pushstring(L, g_base_info_get_name(info));
  lua_concat(L, 3);
  lua_pushvalue(L, -1);
  lua_gettable(L, -3);
  if (!lua_isnil(L, -1))
    {
      lua_replace(L, -4);
      lua_pop(L, 3);
      return 1;
    }

  /* Allocate LgiCallable userdata. */
  nargs = g_callable_info_get_n_args(info);
  callable = lua_newuserdata(L, G_STRUCT_OFFSET(LgiCallable, ffi_args) +
                             sizeof(ffi_type) * (nargs + 3));
  luaL_getmetatable(L, LGI_CALLABLE);
  lua_setmetatable(L, -2);

  /* Fill in callable with proper contents. */
  callable->info = g_base_info_ref(info);
  callable->argc = nargs;
  callable->has_self = 0;
  callable->throws = 0;
  if (GI_IS_FUNCTION_INFO(info))
    {
      /* Get FunctionInfo flags. */
      gint flags = g_function_info_get_flags(info);
      if ((flags & GI_FUNCTION_IS_METHOD) != 0 &&
          (flags & GI_FUNCTION_IS_CONSTRUCTOR) == 0)
        callable->has_self = 1;
      if ((flags & GI_FUNCTION_THROWS) != 0)
        callable->throws = 1;

      /* Resolve symbol (function address). */
      symbol = g_function_info_get_symbol(info);
      if (!g_typelib_symbol(g_base_info_get_typelib(info), symbol,
                            &callable->address))
        /* Fail with the error message. */
        return luaL_error(L, "could not locate %s(%s): %s",
                          lua_tostring(L, -3), symbol, g_module_error());
    }

  /* Store callable object to the cache. */
  lua_pushvalue(L, -3);
  lua_pushvalue(L, -2);
  lua_settable(L, -6);

  /* Final stack cleanup. */
  lua_replace(L, -5);
  lua_pop(L, 3);
  return 1;
}

static int
lgi_callable_gc(lua_State* L)
{
  return 0;
}

const struct luaL_reg lgi_callable_reg[] = {
  { "lgi_callable_gc", lgi_callable_gc },
  { NULL, NULL }
};
