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
  guint nargs : 4;

  /* '1 (retval) + argc + has_self + throws' slots follow. */
  struct 
  {
    /* Index of associated Lua input argument, or 0 if there is no Lua
       arg. */
    guint8 arg_in : 4;
    guint8 arg_out : 4;
  } args[1 /* nargs + 3 */];
} LgiCallable;

/* libffi-related data, appended after LgiCallable (which is also
   variable-length one). */
typedef struct
{
  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* '1 (retval) + argc + has_self + throws' slots follow. */
  ffi_type args[1 /* nargs + 3 */];
} LgiCallableFfi;

int
lgi_callable_store(lua_State* L, GICallableInfo* info)
{
  LgiCallable* callable;
  LgiCallableFfi* ffi;
  gint nargs, argi, inargi, outargi;
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
  callable = 
    lua_newuserdata(L, G_STRUCT_OFFSET(LgiCallable, args[nargs + 3]) +
		    G_STRUCT_OFFSET(LgiCallableFfi, args[nargs + 3]));
  ffi = (LgiCallableFfi*)&callable->args[nargs + 3];
  luaL_getmetatable(L, LGI_CALLABLE);
  lua_setmetatable(L, -2);

  /* Fill in callable with proper contents. */
  callable->info = g_base_info_ref(info);
  callable->nargs = nargs;
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

  /* Go through arguments and fill in args properly. */
  argi = 0;
  inargi = 2;
  outargi = 0;

  /* First of all, check return value. */

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
