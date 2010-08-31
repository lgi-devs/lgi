/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 * License: MIT.
 *
 * This code deals with calling from Lua to C and vice versa, using
 * gobject-introspection information and libffi machinery.
 */

#include "lgi.h"
#include <ffi.h>

/* Represents single parameter in callable description. */
typedef struct _Param
{
  /* Arginfo and Typeinfo instance, initialized, loaded (not dynamically
     allocated). */
  GITypeInfo ti;
  GIArgInfo ai;

  /* Direction of the argument. */
  GIDirection dir;

  /* Ownership passing rule for output parameters. */
  GITransfer transfer;

  /* Flag indicating whether this parameter is represented by Lua input and/or
     returned value.  Not represented are e.g. callback's user_data, array
     sizes etc. */
  gboolean internal;
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
  guint nargs : 6;

  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* ffi return value and pointer to 'nargs + 2' ffi argument slots
     (two additional slots are placeholders for 'self' and 'throw'
     arguments. */
  ffi_type* ffi_retval;
  ffi_type** ffi_args;

  /* Param return value and pointer to nargs Param instances. */
  Param retval;
  Param *params;

  /* ffi_args points here, contains ffi_type[nargs + 2] entries. */
  /* params points here, contains Param[nargs] entries. */
} Callable;

/* Context of single Lua->gobject call. */
typedef struct _Call
{
  /* Callable instance. */
  Callable* callable;

  /* Index of Lua stack where Lua arguments for the method begin. */
  int narg;

  /* Call arguments. */
  GArgument retval;
  GArgument* args;

  /* Argument indirection for OUT and INOUT arguments. */
  GArgument** redirect_out;

  /* libffi argument array. */
  void** ffi_args;

  /* Followed by:
     args -> GArgument[callable->nargs + 1];
     redirect_out -> GArgument*[callable->nargs + 1];
     ffi_args -> void*[callable->nargs + 2]; */
} Call;

/* Structure containing closure data. */
typedef struct _Closure
{
  /* Libffi closure object. */
  ffi_closure ffi_closure;

  /* Lua reference to associated callable. */
  int callable_ref;

  /* Lua reference to target function to be invoked. */
  int target_ref;

  /* Flag indicating whether closure should auto-destroy itself after it is
     called. */
  gboolean autodestroy;
} Closure;

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
		 val_type, val_get, val_set, ffitype)		\
      case tag:							\
	ffi = &ffitype;						\
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
  gint nargs, argi;

  /* Check cache, whether this callable object is already present. */
  luaL_checkstack(L, 5, "");
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_CACHE);
  lua_concat(L, lgi_type_get_name(L, info));
  lua_pushvalue(L, -1);
  lua_gettable(L, -3);
  if (!lua_isnil(L, -1))
    {
      lua_replace(L, -4);
      lua_pop(L, 2);
      return 1;
    }

  /* Allocate Callable userdata. */
  nargs = g_callable_info_get_n_args(info);
  callable = lua_newuserdata(L, sizeof(Callable) +
			     sizeof(ffi_type) * (nargs + 2) +
			     sizeof(Param) * nargs);
  luaL_getmetatable(L, LGI_CALLABLE);
  lua_setmetatable(L, -2);

  /* Fill in callable with proper contents. */
  callable->ffi_args = (ffi_type**)&callable[1];
  callable->params = (Param*)&callable->ffi_args[nargs + 2];
  callable->info = g_base_info_ref(info);
  callable->nargs = nargs;
  callable->has_self = 0;
  callable->throws = 0;
  if (GI_IS_FUNCTION_INFO(info))
    {
      /* Get FunctionInfo flags. */
      const gchar* symbol;
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

  /* Clear all 'internal' flags inside callable parameters, parameters are then
     marked as internal during processing of their parents. */
  for (argi = 0; argi < nargs; argi++)
    callable->params[argi].internal = FALSE;

  /* Process return value. */
  g_callable_info_load_return_type(callable->info, &callable->retval.ti);
  callable->retval.dir = GI_DIRECTION_OUT;
  callable->retval.transfer = g_callable_info_get_caller_owns(callable->info);
  callable->retval.internal = FALSE;
  callable->ffi_retval = get_ffi_type(&callable->retval);

  /* Process 'self' argument, if present. */
  ffi_arg = &callable->ffi_args[0];
  if (callable->has_self)
    *ffi_arg++ = &ffi_type_pointer;

  /* Process the rest of the arguments. */
  param = &callable->params[0];
  for (argi = 0; argi < nargs; argi++, param++, ffi_arg++)
    {
      g_callable_info_load_arg(callable->info, argi, &param->ai);
      g_arg_info_load_type(&param->ai, &param->ti);
      param->dir = g_arg_info_get_direction(&param->ai);
      param->transfer = g_arg_info_get_ownership_transfer(&param->ai);
      *ffi_arg = (param->dir == GI_DIRECTION_IN) ?
	get_ffi_type(param) : &ffi_type_pointer;

      /* If this is callback, mark user_data and destroy_notify as internal. */
      if (g_arg_info_get_scope(&param->ai) != GI_SCOPE_TYPE_INVALID)
        {
          gint arg = g_arg_info_get_closure(&param->ai);
          if (arg > 0 && arg < nargs)
            callable->params[arg - 1].internal = TRUE;
          arg = g_arg_info_get_destroy(&param->ai);
          if (arg > 0 && arg < nargs)
            callable->params[arg - 1].internal = TRUE;
        }
      /* Similarly for array length field. */
      if (g_type_info_get_tag(&param->ti) == GI_TYPE_TAG_ARRAY &&
          g_type_info_get_array_type(&param->ti) == GI_ARRAY_TYPE_C)
        {
          gint arg = g_type_info_get_array_length(&param->ti);
          if (arg > 0 && arg < nargs)
            callable->params[arg - 1].internal = TRUE;
        }
    }

  /* Add ffi info for 'err' argument. */
  if (callable->throws)
    *ffi_arg++ = &ffi_type_pointer;

  /* Create ffi_cif. */
  if (ffi_prep_cif(&callable->cif, FFI_DEFAULT_ABI,
		   callable->has_self + nargs + callable->throws,
		   callable->ffi_retval, callable->ffi_args) != FFI_OK)
    {
      lua_concat(L, lgi_type_get_name(L, callable->info));
      return luaL_error(L, "ffi_prep_cif for `%s' failed",
			lua_tostring(L, -1));
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

/* Closure callback, called by libffi when C code wants to invoke Lua
   callback. */
static void closure_callback(ffi_cif* cif, void* ret, void** args,
                             void* closure)
{
}

/* Destroys specified closure. */
void lgi_closure_destroy(gpointer user_data)
{
  lua_State* L = lgi_main_thread_state;
  Closure* closure = user_data;
  luaL_unref(L, LUA_REGISTRYINDEX, closure->callable_ref);
  luaL_unref(L, LUA_REGISTRYINDEX, closure->target_ref);
  ffi_closure_free(closure);
}

/* Creates closure from Lua function to be passed to C. */
gpointer
lgi_closure_create(lua_State* L, GICallableInfo* ci, int target,
                   gboolean autodestroy, gpointer* call_addr)
{
  Closure* closure;
  Callable* callable;

  /* Allocate closure space. */
  closure = ffi_closure_alloc(sizeof(Closure), call_addr);

  /* Prepare callable and store reference to it. */
  lgi_callable_create(L, ci);
  callable = lua_touserdata(L, -1);
  closure->callable_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  /* Store reference to target Lua function. */
  lua_pushvalue(L, target);
  closure->target_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  /* Remember whether closure should destroy itself automatically after being
     invoked. */
  closure->autodestroy = autodestroy;

  /* Create closure. */
  if (ffi_prep_closure_loc(&closure->ffi_closure, &callable->cif,
                           closure_callback, closure, *call_addr) != FFI_OK)
    {
      lgi_closure_destroy(closure);
      lua_concat(L, lgi_type_get_name(L, ci));
      luaL_error(L, "failed to prepare closure for `%'", lua_tostring(L, -1));
      return NULL;
    }

  return closure;
}

int
lgi_callable_call(lua_State* L, gpointer addr, int func_index, int args_index)
{
  Call* call;
  Param* param;
  int i, lua_argi, nret;
  GError* err = NULL;
  Callable* callable = luaL_checkudata(L, func_index, LGI_CALLABLE);

  /* We cannot push more stuff than count of arguments we have. */
  luaL_checkstack(L, callable->nargs, "");

  /* Check that we know where to call. */
  if (addr == NULL)
    {
      addr = callable->address;
      if (addr == NULL)
	{
	  lua_concat(L, lgi_type_get_name(L, callable->info));
	  return luaL_error(L, "`%s': no native addr to call",
			    lua_tostring(L, -1));
	}
    }

  /* Allocate (on the stack) and fill in Call instance. */
  call = g_alloca(sizeof(Call) +
		  sizeof(GArgument) * (callable->nargs + 1) * 2 +
		  sizeof(void*) * (callable->nargs + 2));
  call->callable = callable;
  call->narg = args_index;
  call->args = (GArgument*)&call[1];
  call->redirect_out = (GArgument**)&call->args[callable->nargs + 1];
  call->ffi_args = (void**)&call->redirect_out[callable->nargs + 1];

  /* Prepare 'self', if present. */
  lua_argi = args_index;
  if (callable->has_self)
    {
      call->args[0].v_pointer =
	lgi_compound_get(L, args_index,
			 g_base_info_get_container(callable->info), TRUE);
      call->ffi_args[0] = &call->args[0];
      lua_argi++;
    }

  /* Process input parameters. */
  nret = 0;
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    {
      /* Prepare ffi_args and redirection for out/inout parameters. */
      int argi = i + callable->has_self;
      if (param->dir == GI_DIRECTION_IN)
	call->ffi_args[argi] = &call->args[argi];
      else
	{
	  call->ffi_args[argi] = &call->redirect_out[argi];
	  call->redirect_out[argi] = &call->args[argi];
	}

      if (!param->internal)
        {
          if (param->dir != GI_DIRECTION_OUT)
            /* Convert parameter from Lua stack to C. */
            nret += lgi_marshal_2c(L, &param->ti, &param->ai, &call->args[argi],
                                   lua_argi++, callable->info, call->args);
          else
            {
              /* Special handling for out/caller-alloc structures; we have to
                 manually pre-create them and store them on the stack. */
              if (g_arg_info_is_caller_allocates(&param->ai) &&
                  g_type_info_get_tag(&param->ti) == GI_TYPE_TAG_INTERFACE)
                {
                  GIBaseInfo* ii = g_type_info_get_interface(&param->ti);
                  if (g_base_info_get_type(ii) == GI_INFO_TYPE_STRUCT)
                    lgi_compound_create_struct(L, ii,
                                               &call->args[argi].v_pointer);
                  g_base_info_unref(ii);
                }
            }
        }
    }

  /* Add error for 'throws' type function. */
  if (callable->throws)
      call->ffi_args[callable->has_self + callable->nargs] = &err;

  /* Call the function. */
  ffi_call(&callable->cif, addr, &call->retval, call->ffi_args);

  /* Pop any temporary items from the stack which might be stored there by
     marshalling code. */
  lua_pop(L, nret);

  /* Check, whether function threw. */
  if (err != NULL)
    return lgi_error(L, err);

  /* Handle return value. */
  nret = 0;
  if (g_type_info_get_tag(&callable->retval.ti) != GI_TYPE_TAG_VOID)
    nret = lgi_marshal_2lua(L, &callable->retval.ti, &call->retval,
			    callable->retval.transfer, callable->info,
                            call->args) ? 1 : 0;

  /* Process output parameters. */
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    if (!param->internal && param->dir != GI_DIRECTION_IN)
      if (lgi_marshal_2lua(L, &param->ti, &call->args[i],
			   param->transfer, callable->info, call->args))
	nret++;

  return nret;
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
