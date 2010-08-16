/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 * License: MIT.
 *
 * This code deals with calling from Lua to C and vice versa, using
 * gobject-introspection information and libffi machinery.  Basically this is
 * the complex part of Lgi.
 */

#include "lgi.h"

#include <ffi.h>

/* Represents single parameter in callable description. */
typedef struct _Param
{
  /* Typeinfo instance, initialzed, loaded (not dynamically
     allocated).  Only for 'self' parameters this instance is
     unused. */
  GITypeInfo ti;

  /* Other information about parameter. */
  GIDirection dir;
  GITransfer transfer;
  gboolean caller_alloc;
  gboolean optional;

  /* Index of input and output argument on Lua stack (1-based, 0 means
     that this parameter has not associated Lua value) */
  int nval_in;
  int nval_out;
} Param;

/* Structure representing userdata allocated for any callable, i.e. function,
   method, signal, vtable, callback... */
typedef struct _Callable
{
  /* Stored callable info. */
  GICallableInfo* info;

  /* Address of the function, if target is IFunctionInfo. */
  gpointer address;

  /* Flags with function characteristics. */
  guint has_self : 1;
  guint throws : 1;
  guint optional : 1;
  guint nargs : 6;

  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* Pointer to 'nargs + 3' ffi argument slots. */
  ffi_type** ffi_args;

  /* Pointer to 'nargs + 2' Param instances. */
  Param *params;

  /* ffi_args points here, contains ffi_type[nargs + 3] entries. */
  /* params points here, contains Param[nargs + 2] entries. */
} Callable;

/* Gets ffi_type for given tag, returns NULL if it cannot be handled. */
static ffi_type*
get_simple_ffi_type(GITypeTag tag)
{
  ffi_type* ffi;
  switch (tag)
    {
    case GI_TYPE_TAG_VOID:
      ffi = &ffi_type_void;
      break;

#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 val_type, val_get, val_set, ffitype)           \
      case tag:                                                 \
	ffi = &ffitype;                                         \
	break;
#include "decltype.h"

    default:
      ffi = NULL;
    }

  return ffi;
}

/* Gets ffi_type for given Param instance. */
static ffi_type*
get_ffi_type(Param* param)
{
  /* In case of inout or out parameters, the type is always pointer. */
  GITypeTag tag = g_type_info_get_tag(&param->ti);
  ffi_type* ffi = get_simple_ffi_type(tag);
  if (ffi == NULL)
    {
      /* Something more complex. */
      if (tag == GI_TYPE_TAG_INTERFACE)
	{
	  GIBaseInfo* ii = g_type_info_get_interface(&param->ti);
	  switch (g_base_info_get_type(ii))
	    {
	    case GI_INFO_TYPE_ENUM:
	    case GI_INFO_TYPE_FLAGS:
	      ffi = get_simple_ffi_type(g_enum_info_get_storage_type(ii));
	      break;

	    default:
	      break;
	    }
	  g_base_info_unref(ii);
	}
    }

  return ffi != NULL ? ffi : &ffi_type_pointer;
}

int
lgi_callable_create(lua_State* L, GICallableInfo* info)
{
  Callable* callable;
  Param* param;
  ffi_type** ffi_arg;
  gint nargs, argi, in_argi, out_argi;
  GIArgInfo ai;
  const gchar* symbol;

  /* Check cache, whether this callable object is already present. */
  luaL_checkstack(L, 5, "");
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

  /* Allocate Callable userdata. */
  nargs = g_callable_info_get_n_args(info);
  callable = lua_newuserdata(L, sizeof(Callable) +
			     sizeof(ffi_type) * (nargs + 3) +
			     sizeof(Param) * (nargs + 2));
  luaL_getmetatable(L, LGI_CALLABLE);
  lua_setmetatable(L, -2);

  /* Fill in callable with proper contents. */
  callable->ffi_args = (ffi_type**)&callable[1];
  callable->params = (Param*)&callable->ffi_args[nargs + 3];
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

  /* Process return value. */
  param = &callable->params[0];
  ffi_arg = &callable->ffi_args[0];
  g_callable_info_load_return_type(callable->info, &param->ti);
  param->transfer = g_callable_info_get_caller_owns(callable->info);
  param->caller_alloc = FALSE;
  param->optional = FALSE;
  *ffi_arg = get_ffi_type(param);
  param->dir = GI_DIRECTION_OUT;
  param->nval_in = 0;
  param->nval_out = (callable->ffi_args[0] == &ffi_type_void) ? 0 : -1;
  param++;
  ffi_arg++;

  /* Process 'self' argument, if present. */
  if (callable->has_self)
    {
      callable->ffi_args[1] = &ffi_type_pointer;
      param->nval_in = -1;
      param->nval_out = 0;
      param++;
      ffi_arg++;
    }

  /* Process the rest of the arguments. */
  for (argi = 0; argi < nargs; argi++, param++, ffi_arg++)
    {
      g_callable_info_load_arg(callable->info, argi, &ai);
      g_arg_info_load_type(&ai, &param->ti);
      param->dir = g_arg_info_get_direction(&ai);
      param->transfer = g_arg_info_get_ownership_transfer(&ai);
      param->caller_alloc = g_arg_info_is_caller_allocates(&ai);
      param->optional = g_arg_info_is_optional(&ai) ||
	g_arg_info_may_be_null(&ai);
      switch (param->dir)
	{
	case GI_DIRECTION_IN:
	  *ffi_arg = get_ffi_type(param);
	  param->nval_in = -1;
	  param->nval_out = 0;
	  break;

	case GI_DIRECTION_OUT:
	  *ffi_arg = &ffi_type_pointer;
	  param->nval_in = 0;
	  param->nval_out = -1;
	  break;

	case GI_DIRECTION_INOUT:
	  *ffi_arg = &ffi_type_pointer;
	  param->nval_in = -1;
	  param->nval_out = -1;
	  break;
	}
    }

  /* Add ffi info for 'err' argument. */
  if (callable->throws)
    *ffi_arg++ = &ffi_type_pointer;

  /* Create ffi_cif. */
  if (ffi_prep_cif(&callable->cif, FFI_DEFAULT_ABI, 
		   1 + callable->has_self + nargs + callable->throws,
		   callable->ffi_args[0], &callable->ffi_args[1]) != FFI_OK)
    {
      lua_concat(L, lgi_type_get_name(L, callable->info));
      return luaL_error(L, "ffi_prep_cif for `%s' failed",
			lua_tostring(L, -1));
    }

  /* Process callable[args] and reassign narg and nret fields, so that instead
     of 0/-1 they contain real index of lua argument on the stack. */
  for (argi = 0, in_argi = out_argi = 1; argi < nargs + 1 + callable->has_self; 
       argi++)
    {
      if (callable->params[argi].nval_in != 0)
	callable->params[argi].nval_in = in_argi++;

      if (callable->params[argi].nval_out != 0)
	callable->params[argi].nval_out = out_argi++;
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

int
lgi_callable_call(lua_State* L, gpointer addr, int func_index, int args_index)
{
  Callable* callable = luaL_checkudata(L, func_index, LGI_CALLABLE);

  /* Check that we know where to call. */
  if (addr == NULL && callable->address == NULL)
    {
      lua_concat(L, lgi_type_get_name(L, callable->info));
      return luaL_error(L, "`%s': no native addr to call",
			lua_tostring(L, -1));
    }

  return 0;
}

static int
callable_gc(lua_State* L)
{
  /* Just unref embedded 'info' field. */
  Callable* callable = luaL_checkudata(L, 1, LGI_CALLABLE);
  g_base_info_unref(callable->info);
  return 0;
}

static int
callable_tostring(lua_State* L)
{
  Callable* callable = luaL_checkudata(L, 1, LGI_CALLABLE);
  lua_pushfstring(L, "lgi.%s (%p): ",
		  (GI_IS_FUNCTION_INFO(callable->info) ? "fun" :
		   (GI_IS_SIGNAL_INFO(callable->info) ? "sig" :
		    (GI_IS_VFUNC_INFO(callable->info) ? "vfn" : "cbk"))),
		  callable->address);
  lua_concat(L, lgi_type_get_name(L, callable->info) + 1);
  return 1;
}

static int
callable_call(lua_State* L)
{
  return lgi_callable_call(L, NULL, 1, 2);
}

const struct luaL_reg lgi_callable_reg[] = {
  { "__gc", callable_gc },
  { "__tostring", callable_tostring },
  { "__call", callable_call },
  { NULL, NULL }
};
