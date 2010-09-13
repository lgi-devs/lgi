/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
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
  guint dir : 2;

  /* Ownership passing rule for output parameters. */
  guint transfer : 2;

  /* Flag indicating whether this parameter is represented by Lua input and/or
     returned value.  Not represented are e.g. callback's user_data, array
     sizes etc. */
  guint internal : 1;
} Param;

/* Structure representing userdata allocated for any callable, i.e. function,
   method, signal, vtable, callback... */
typedef struct _Callable
{
  /* Stored callable info. */
  GICallableInfo *info;

  /* Address of the function, if target is IFunctionInfo. */
  gpointer address;

  /* Flags with function characteristics. */
  guint has_self : 1;
  guint throws : 1;
  guint nargs : 6;

  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* Param return value and pointer to nargs Param instances. */
  Param retval;
  Param *params;

  /* ffi_type* array here, contains ffi_type[nargs + 2] entries. */
  /* params points here, contains Param[nargs] entries. */
} Callable;

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
static ffi_type *
get_simple_ffi_type (GITypeTag tag)
{
  ffi_type *ffi;
  switch (tag)
    {
    case GI_TYPE_TAG_VOID:
      ffi = &ffi_type_void;
      break;

#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
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
static ffi_type *
get_ffi_type(Param *param)
{
  /* In case of inout or out parameters, the type is always pointer. */
  GITypeTag tag = g_type_info_get_tag (&param->ti);
  ffi_type* ffi = get_simple_ffi_type (tag);
  if (ffi == NULL)
    {
      /* Something more complex. */
      if (tag == GI_TYPE_TAG_INTERFACE)
	{
	  GIBaseInfo *ii = g_type_info_get_interface (&param->ti);
	  switch (g_base_info_get_type (ii))
	    {
	    case GI_INFO_TYPE_ENUM:
	    case GI_INFO_TYPE_FLAGS:
	      ffi = get_simple_ffi_type (g_enum_info_get_storage_type (ii));
	      break;

	    default:
	      break;
	    }
	  g_base_info_unref (ii);
	}
    }

  return ffi != NULL ? ffi : &ffi_type_pointer;
}

int
lgi_callable_create (lua_State *L, GICallableInfo *info)
{
  Callable *callable;
  Param *param;
  ffi_type **ffi_arg, **ffi_args;
  ffi_type *ffi_retval;
  gint nargs, argi, arg;

  /* Check cache, whether this callable object is already present. */
  luaL_checkstack (L, 5, "");
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti (L, -1, LGI_REG_CACHE);
  lua_concat (L, lgi_type_get_name(L, info));
  lua_pushvalue (L, -1);
  lua_gettable (L, -3);
  if (!lua_isnil (L, -1))
    {
      lua_replace (L, -4);
      lua_pop (L, 2);
      return 1;
    }

  /* Allocate Callable userdata. */
  nargs = g_callable_info_get_n_args (info);
  callable = lua_newuserdata (L, sizeof (Callable) +
                              sizeof (ffi_type) * (nargs + 2) +
                              sizeof (Param) * nargs);
  luaL_getmetatable (L, LGI_CALLABLE);
  lua_setmetatable (L, -2);

  /* Fill in callable with proper contents. */
  ffi_args = (ffi_type **) &callable[1];
  callable->params = (Param *) &ffi_args[nargs + 2];
  callable->info = g_base_info_ref (info);
  callable->nargs = nargs;
  callable->has_self = 0;
  callable->throws = 0;
  if (GI_IS_FUNCTION_INFO (info))
    {
      /* Get FunctionInfo flags. */
      const gchar* symbol;
      gint flags = g_function_info_get_flags (info);
      if ((flags & GI_FUNCTION_IS_METHOD) != 0 &&
	  (flags & GI_FUNCTION_IS_CONSTRUCTOR) == 0)
	callable->has_self = 1;
      if ((flags & GI_FUNCTION_THROWS) != 0)
	callable->throws = 1;

      /* Resolve symbol (function address). */
      symbol = g_function_info_get_symbol (info);
      if (!g_typelib_symbol (g_base_info_get_typelib (info), symbol,
                             &callable->address))
	/* Fail with the error message. */
	return luaL_error (L, "could not locate %s(%s): %s",
                           lua_tostring (L, -3), symbol, g_module_error ());
    }

  /* Clear all 'internal' flags inside callable parameters, parameters are then
     marked as internal during processing of their parents. */
  for (argi = 0; argi < nargs; argi++)
    callable->params[argi].internal = FALSE;

  /* Process return value. */
  g_callable_info_load_return_type (callable->info, &callable->retval.ti);
  callable->retval.dir = GI_DIRECTION_OUT;
  callable->retval.transfer = g_callable_info_get_caller_owns (callable->info);
  callable->retval.internal = FALSE;
  ffi_retval = get_ffi_type (&callable->retval);

  /* Process 'self' argument, if present. */
  ffi_arg = &ffi_args[0];
  if (callable->has_self)
    *ffi_arg++ = &ffi_type_pointer;

  /* Process the rest of the arguments. */
  param = &callable->params[0];
  for (argi = 0; argi < nargs; argi++, param++, ffi_arg++)
    {
      g_callable_info_load_arg (callable->info, argi, &param->ai);
      g_arg_info_load_type (&param->ai, &param->ti);
      param->dir = g_arg_info_get_direction (&param->ai);
      param->transfer = g_arg_info_get_ownership_transfer (&param->ai);
      *ffi_arg = (param->dir == GI_DIRECTION_IN) ?
	get_ffi_type(param) : &ffi_type_pointer;

      /* Mark closure-related user_data fields and possibly destroy_notify
         fields as internal. */
      arg = g_arg_info_get_closure (&param->ai);
      if (arg > 0 && arg < nargs)
        callable->params[arg].internal = TRUE;
      arg = g_arg_info_get_destroy (&param->ai);
      if (arg > 0 && arg < nargs)
        callable->params[arg].internal = TRUE;

      /* Similarly for array length field. */
      if (g_type_info_get_tag (&param->ti) == GI_TYPE_TAG_ARRAY &&
	  g_type_info_get_array_type (&param->ti) == GI_ARRAY_TYPE_C)
	{
	  arg = g_type_info_get_array_length (&param->ti);
	  if (arg > 0 && arg < nargs)
	    callable->params[arg - 1].internal = TRUE;
	}
    }

  /* Add ffi info for 'err' argument. */
  if (callable->throws)
    *ffi_arg++ = &ffi_type_pointer;

  /* Create ffi_cif. */
  if (ffi_prep_cif (&callable->cif, FFI_DEFAULT_ABI,
                    callable->has_self + nargs + callable->throws,
                    ffi_retval, ffi_args) != FFI_OK)
    {
      lua_concat (L, lgi_type_get_name (L, callable->info));
      return luaL_error (L, "ffi_prep_cif for `%s' failed",
                         lua_tostring (L, -1));
    }

  /* Store callable object to the cache. */
  lua_pushvalue (L, -3);
  lua_pushvalue (L, -2);
  lua_settable (L, -6);

  /* Final stack cleanup. */
  lua_replace (L, -5);
  lua_pop (L, 3);
  return 1;
}

int
lgi_callable_call (lua_State *L, gpointer addr, int func_index, int args_index)
{
  Param *param;
  int i, lua_argi, nret;
  GIArgument retval;
  GIArgument *args;
  void **ffi_args, **redirect_out;
  GError *err = NULL;
  Callable *callable = luaL_checkudata (L, func_index, LGI_CALLABLE);

  /* We cannot push more stuff than count of arguments we have. */
  luaL_checkstack (L, callable->nargs, "");

  /* Check that we know where to call. */
  if (addr == NULL)
    {
      addr = callable->address;
      if (addr == NULL)
	{
	  lua_concat (L, lgi_type_get_name (L, callable->info));
	  return luaL_error (L, "`%s': no native addr to call",
                             lua_tostring (L, -1));
	}
    }

  /* Prepare data for the call. */
  args = g_newa (GIArgument, callable->nargs + callable->has_self);
  redirect_out = g_newa (void *, callable->nargs + callable->has_self);
  ffi_args = g_newa (void *, callable->nargs + callable->has_self);

  /* Prepare 'self', if present. */
  lua_argi = args_index;
  if (callable->has_self)
    {
      args[0].v_pointer =
          lgi_compound_get (L, args_index,
                            g_base_info_get_container (callable->info), FALSE);
      ffi_args[0] = &args[0];
      lua_argi++;
    }

  /* Prepare proper call->ffi_args[] pointing to real args (or redirects in
     case of inout/out parameters). */
  nret = 0;
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    {
      /* Prepare ffi_args and redirection for out/inout parameters. */
      int argi = i + callable->has_self;
      if (param->dir == GI_DIRECTION_IN)
	ffi_args[argi] = &args[argi];
      else
	{
	  ffi_args[argi] = &redirect_out[argi];
	  redirect_out[argi] = &args[argi];
	}
    }

  /* Process input parameters. */
  nret = 0;
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    if (!param->internal)
      {
        int argi = i + callable->has_self;
        if (param->dir != GI_DIRECTION_OUT)
          /* Convert parameter from Lua stack to C. */
          nret += lgi_marshal_2c (L, &param->ti, &param->ai, param->transfer,
                                  &args[argi], lua_argi++,
                                  callable->info,
                                  (GIArgument **) (ffi_args +
                                                   callable->has_self));
        else
          {
            /* Special handling for out/caller-alloc structures; we have to
               manually pre-create them and store them on the stack. */
            if (g_arg_info_is_caller_allocates (&param->ai) &&
                g_type_info_get_tag (&param->ti) == GI_TYPE_TAG_INTERFACE)
              {
                GIBaseInfo *ii = g_type_info_get_interface (&param->ti);
                GIInfoType type = g_base_info_get_type (ii);
                if (type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_UNION)
                  args[argi].v_pointer = lgi_compound_struct_new (L, ii);
                g_base_info_unref (ii);
              }
          }
      }

  /* Add error for 'throws' type function. */
  if (callable->throws)
    ffi_args[callable->has_self + callable->nargs] = &err;

  /* Call the function. */
  ffi_call (&callable->cif, addr, &retval, ffi_args);

  /* Pop any temporary items from the stack which might be stored there by
     marshalling code. */
  lua_pop (L, nret);

  /* Check, whether function threw. */
  if (err != NULL)
    return lgi_error (L, err);

  /* Handle return value. */
  nret = 0;
  if (g_type_info_get_tag (&callable->retval.ti) != GI_TYPE_TAG_VOID)
    nret = lgi_marshal_2lua (L, &callable->retval.ti, &retval,
                             callable->retval.transfer, callable->info, NULL)
        ? 1 : 0;

  /* Process output parameters. */
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    if (!param->internal && param->dir != GI_DIRECTION_IN)
      if (lgi_marshal_2lua (L, &param->ti, &args[i + callable->has_self],
			   param->transfer, callable->info,
                           (GIArgument**) (ffi_args +
                                           callable->has_self)))
	nret++;

  return nret;
}

static int
callable_gc (lua_State *L)
{
  /* Just unref embedded 'info' field. */
  Callable *callable = luaL_checkudata (L, 1, LGI_CALLABLE);
  g_base_info_unref (callable->info);
  return 0;
}

static int
callable_tostring (lua_State *L)
{
  Callable *callable = luaL_checkudata (L, 1, LGI_CALLABLE);
  lua_pushfstring (L, "lgi.%s (%p): ",
                   (GI_IS_FUNCTION_INFO (callable->info) ? "fun" :
                    (GI_IS_SIGNAL_INFO (callable->info) ? "sig" :
                     (GI_IS_VFUNC_INFO (callable->info) ? "vfn" : "cbk"))),
		  callable->address);
  lua_concat (L, lgi_type_get_name (L, callable->info) + 1);
  return 1;
}

static int
callable_call (lua_State *L)
{
  return lgi_callable_call (L, NULL, 1, 2);
}

const struct luaL_reg lgi_callable_reg[] = {
  { "__gc", callable_gc },
  { "__tostring", callable_tostring },
  { "__call", callable_call },
  { NULL, NULL }
};

/* Closure callback, called by libffi when C code wants to invoke Lua
   callback. */
static void
closure_callback (ffi_cif *cif, void *ret, void **args, void *closure_arg)
{
  Callable *callable;
  Closure *closure = closure_arg;
  gint res, npos, i;
  Param *param;

  /* Get access to proper Lua context. */
  lua_State *L = lgi_main_thread_state;

  /* Get access to Callable structure. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, closure->callable_ref);
  callable = lua_touserdata (L, -1);
  lua_pop (L, 1);

  /* Push function (target) to be called to the stack. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, closure->target_ref);

  /* Marshall 'self' argument, if it is present. */
  npos = 0;
  if (callable->has_self)
    {
      if (lgi_compound_create (L, g_base_info_get_container(callable->info),
                               ((GIArgument*) args[0])->v_pointer, FALSE))
        npos++;
    }

  /* Marshal input arguments to lua. */
  param = callable->params;
  for (i = 0; i < callable->nargs; ++i, ++param)
    if (!param->internal && param->dir != GI_DIRECTION_OUT)
      {
        if (lgi_marshal_2lua (L, &param->ti,
                              (GIArgument *) args[i + callable->has_self],
                              param->transfer, callable->info,
                              (GIArgument **) &args[callable->has_self]))
            npos++;
      }

  /* Call it. */
#ifndef NDEBUG
  lua_concat (L, lgi_type_get_name (L, callable->info));
  g_debug ("invoking closure %s/%p/(%d args), stack=%s",
           lua_tostring (L, -1), closure, npos, lgi_sd (L));
  lua_pop (L, 1);
#endif
  res = lua_pcall (L, npos, LUA_MULTRET, 0);
  npos = 1;

  /* Check, whether we can report an error here. */
  if (res == 0)
    {
      /* Marshal return value from Lua. */
      if (g_type_info_get_tag (&callable->retval.ti) != GI_TYPE_TAG_VOID)
	{
	  lgi_marshal_2c (L, &callable->retval.ti, NULL,
                          callable->retval.transfer, ret, npos, NULL, NULL);
	  npos++;
	}

      /* Marshal output arguments from Lua. */
      param = callable->params;
      for (i = 0; i < callable->nargs; ++i, ++param)
        if (!param->internal && param->dir != GI_DIRECTION_IN)
          {
            lgi_marshal_2c (L, &param->ti, &param->ai, param->transfer,
                            (GIArgument *)args[i + callable->has_self], npos,
                            callable->info,
                            (GIArgument **)(args + callable->has_self));
            npos++;
          }
    }
  else if (callable->throws)
    {
      /* If the function is expected to return errors, create proper error. */
      GQuark q = g_quark_from_static_string ("lgi-callback-error-quark");
      GError **err = ((GIArgument *) args[callable->has_self +
                                          callable->nargs])->v_pointer;
      g_set_error_literal (err, q, 1, lua_tostring(L, -1));
      lua_pop (L, 1);
    }

  /* If the closure is marked as autodestroy, destroy it now.  Note that it is
     unfortunately not possible to destroy it directly here, because we would
     delete the code under our feet and crash and burn :-(. Instead, we create
     marshal guard and leave it to GC to destroy the closure later. */
  if (closure->autodestroy)
    {
      lgi_closure_guard (L, closure);
      lua_pop (L, 1);
    }
}

/* Destroys specified closure. */
void
lgi_closure_destroy (gpointer user_data)
{
  lua_State* L = lgi_main_thread_state;
  Closure* closure = user_data;

#ifndef NDEBUG
  g_debug ("destroying closure %p", closure);
#endif
  luaL_unref (L, LUA_REGISTRYINDEX, closure->callable_ref);
  luaL_unref (L, LUA_REGISTRYINDEX, closure->target_ref);
  ffi_closure_free (closure);
}

/* Creates closure from Lua function to be passed to C. */
gpointer
lgi_closure_create (lua_State *L, GICallableInfo *ci, int target,
                    gboolean autodestroy, gpointer *call_addr)
{
  Closure *closure;
  Callable *callable;

  /* Prepare callable and store reference to it. */
  lgi_callable_create (L, ci);
  callable = lua_touserdata (L, -1);

  /* Allocate closure space. */
  closure = ffi_closure_alloc (sizeof (Closure), call_addr);
  closure->callable_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Store reference to target Lua function. */
  lua_pushvalue (L, target);
  closure->target_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Remember whether closure should destroy itself automatically after being
     invoked. */
  closure->autodestroy = autodestroy;

  /* Create closure. */
  if (ffi_prep_closure_loc (&closure->ffi_closure, &callable->cif,
                            closure_callback, closure, *call_addr) != FFI_OK)
    {
      lgi_closure_destroy (closure);
      lua_concat (L, lgi_type_get_name (L, ci));
      luaL_error (L, "failed to prepare closure for `%'", lua_tostring (L, -1));
      return NULL;
    }

#ifndef NDEBUG
  lua_concat (L, lgi_type_get_name (L, ci));
  g_debug("created closure %p(%s)", closure, lua_tostring (L, -1));
  lua_pop (L, 1);
#endif
  return closure;
}

static int
closureguard_gc(lua_State *L)
{
  gpointer closure = *(gpointer *) lua_touserdata (L, 1);
  lgi_closure_destroy (closure);
  return 0;
}

const struct luaL_reg lgi_closureguard_reg[] = {
  { "__gc", closureguard_gc },
  { NULL, NULL }
};

void
lgi_closure_guard (lua_State *L, gpointer user_data)
{
  gpointer *closureguard;
  luaL_checkstack (L, 1, "");
  closureguard = lua_newuserdata (L, sizeof (gpointer));
  *closureguard = user_data;
  luaL_getmetatable (L, LGI_CLOSUREGUARD);
  lua_setmetatable (L, -2);
}
