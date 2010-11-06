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
#define UD_CALLABLE "lgi.callable"

/* Structure containing basic callback information. */
typedef struct _Callback
{
  /* Thread which created callback and Lua-reference to it (so that it
     is not GCed). */
  lua_State *L;
  int thread_ref;

  /* Callable's target to be invoked (either function, userdata/table
     with __call metafunction or coroutine (which is resumed instead
     of called). */
  int target_ref;
} Callback;

/* Structure containing closure data. */
typedef struct _FfiClosure
{
  /* Libffi closure object. */
  ffi_closure ffi_closure;

  /* Lua reference to associated callable. */
  int callable_ref;

  /* Target to be invoked. */
  Callback callback;

  /* Flag indicating whether closure should auto-destroy itself after it is
     called. */
  gboolean autodestroy;
} FfiClosure;

/* Gets ffi_type for given tag, returns NULL if it cannot be handled. */
static ffi_type *
get_simple_ffi_type (GITypeTag tag)
{
  ffi_type *ffi;
  switch (tag)
    {
#define HANDLE_TYPE(tag, ffitype)		\
      case GI_TYPE_TAG_ ## tag:			\
	ffi = &ffi_type_ ## ffitype;		\
	break

      HANDLE_TYPE(VOID, void);
      HANDLE_TYPE(BOOLEAN, uint);
      HANDLE_TYPE(INT8, sint8);
      HANDLE_TYPE(UINT8, uint8);
      HANDLE_TYPE(INT16, sint16);
      HANDLE_TYPE(UINT16, uint16);
      HANDLE_TYPE(INT32, sint32);
      HANDLE_TYPE(UINT32, uint32);
      HANDLE_TYPE(INT64, sint64);
      HANDLE_TYPE(UINT64, uint64);
      HANDLE_TYPE(FLOAT, float);
      HANDLE_TYPE(DOUBLE, double);
#if GLIB_SIZEOF_SIZE_T == 4
      HANDLE_TYPE(GTYPE, uint32);
#else
      HANDLE_TYPE(GTYPE, uint64);
#endif
#undef HANDLE_TYPE

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

/* If typeinfo specifies array with length parameter, mark it in
   specified callable as an internal one. */
static void
callable_mark_array_length (Callable *callable, GITypeInfo *ti)
{
  gint arg;
  if (g_type_info_get_tag (ti) == GI_TYPE_TAG_ARRAY &&
      g_type_info_get_array_type (ti) == GI_ARRAY_TYPE_C)
    {
      arg = g_type_info_get_array_length (ti);
      if (arg >= 0 && arg < callable->nargs)
	callable->params[arg].internal = TRUE;
    }
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
  lua_pushinteger (L, g_base_info_get_type (info));
  lua_pushstring (L, ":");
  lua_concat (L, lgi_type_get_name(L, info) + 2);
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
  luaL_getmetatable (L, UD_CALLABLE);
  lua_setmetatable (L, -2);

  /* Fill in callable with proper contents. */
  ffi_args = (ffi_type **) &callable[1];
  callable->params = (Param *) &ffi_args[nargs + 2];
  callable->info = g_base_info_ref (info);
  callable->nargs = nargs;
  callable->has_self = 0;
  callable->throws = 0;
  callable->address = NULL;
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
  else if (GI_IS_SIGNAL_INFO (info))
    /* Signals always have 'self', i.e. the object on which they are
       emitted. */
    callable->has_self = 1;

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
  callable_mark_array_length (callable, &callable->retval.ti);

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
      callable_mark_array_length (callable, &param->ti);
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
  int i, lua_argi, nret, caller_allocated = 0, nargs;
  GIArgument retval;
  GIArgument *args;
  void **ffi_args, **redirect_out;
  GError *err = NULL;
  Callable *callable = luaL_checkudata (L, func_index, UD_CALLABLE);

  /* Make sure that all unspecified arguments are set as nil; during
     marhsalling we might create temporary values on the stack, which
     can be confused with input arguments expected but not passed by
     caller. */
  lua_settop(L, callable->has_self + callable->nargs + 1);

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
  nargs = callable->nargs + callable->has_self;
  args = g_newa (GIArgument, nargs);
  redirect_out = g_newa (void *, nargs + callable->throws);
  ffi_args = g_newa (void *, nargs + callable->throws);

  /* Prepare 'self', if present. */
  lua_argi = args_index;
  nret = 0;
  if (callable->has_self)
    {
      GIBaseInfo *parent = g_base_info_get_container (callable->info);
      GType parent_gtype = g_registered_type_info_get_g_type (parent);
      nret += lgi_compound_get (L, args_index, &parent_gtype,
				&args[0].v_pointer, 0);
      ffi_args[0] = &args[0];
      lua_argi++;
    }

  /* Prepare proper call->ffi_args[] pointing to real args (or
     redirects in case of inout/out parameters). Note that this loop
     cannot be merged with following marshalling loop, because during
     marshalling of closure or arrays marshalling code can read/write
     values ahead of currently marshalled value. */
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
	  nret += lgi_marshal_arg_2c (L, &param->ti, &param->ai,
				      GI_TRANSFER_NOTHING,
				      &args[argi], lua_argi++, FALSE,
				      callable->info,
				      ffi_args + callable->has_self);
	/* Special handling for out/caller-alloc structures; we have to
	   manually pre-create them and store them on the stack. */
	else if (g_arg_info_is_caller_allocates (&param->ai)
		 && lgi_marshal_arg_2c_caller_alloc (L, &param->ti,
						     &args[argi], 0))
	  {
	    /* Even when marked as OUT, caller-allocates arguments
	       behave as if they are actually IN from libffi POV. */
	    ffi_args[argi] = &args[argi];

	    /* Move the value on the stack *below* any already present
	       temporary values. */
	    lua_insert (L, -nret - 1);
	    caller_allocated++;
	  }
      }

  /* Add error for 'throws' type function. */
  if (callable->throws)
    {
      redirect_out[nargs] = &err;
      ffi_args[nargs] = &redirect_out[nargs];
    }

  /* Call the function. */
  ffi_call (&callable->cif, addr, &retval, ffi_args);

  /* Pop any temporary items from the stack which might be stored there by
     marshalling code. */
  lua_pop (L, nret);

  /* Check, whether function threw. */
  if (err != NULL)
    {
      lua_pushboolean (L, 0);
      lua_pushstring (L, err->message);
      lua_pushinteger (L, err->code);
      g_error_free (err);
      return 3;
    }

  /* Handle return value. */
  nret = 0;
  if (g_type_info_get_tag (&callable->retval.ti) != GI_TYPE_TAG_VOID)
    {
      lgi_marshal_arg_2lua (L, &callable->retval.ti, callable->retval.transfer,
			    &retval, 0, FALSE, callable->info,
			    ffi_args + callable->has_self);
      nret++;
      lua_insert (L, -caller_allocated - 1);
    }

  /* Process output parameters. */
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    if (!param->internal && param->dir != GI_DIRECTION_IN)
      {
	if (!g_arg_info_is_caller_allocates (&param->ai))
	  {
	    /* Marshal output parameter. */
	    lgi_marshal_arg_2lua (L, &param->ti, param->transfer,
				  &args[i + callable->has_self], 0, FALSE,
				  callable->info,
				  ffi_args + callable->has_self);
	    lua_insert (L, -caller_allocated - 1);
	  }
	else if (lgi_marshal_arg_2c_caller_alloc (L, &param->ti, NULL,
						  -caller_allocated  - nret))
	  /* Caller allocated parameter is already marshalled and
	     lying on the stack. */
	  caller_allocated--;

	nret++;
      }

  g_assert (caller_allocated == 0);
  return nret;
}

static int
callable_gc (lua_State *L)
{
  /* Just unref embedded 'info' field. */
  Callable *callable = luaL_checkudata (L, 1, UD_CALLABLE);
  g_base_info_unref (callable->info);
  return 0;
}

static int
callable_tostring (lua_State *L)
{
  Callable *callable = luaL_checkudata (L, 1, UD_CALLABLE);
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

static const struct luaL_reg callable_reg[] = {
  { "__gc", callable_gc },
  { "__tostring", callable_tostring },
  { "__call", callable_call },
  { NULL, NULL }
};

/* Initializes target substructure. */
static void
callback_create (lua_State *L, Callback *callback, int target_arg)
{
  /* Store reference to target Lua function. */
  lua_pushvalue (L, target_arg);
  callback->target_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Store reference to target Lua thread. */
  callback->L = L;
  lua_pushthread (L);
  callback->thread_ref = luaL_ref (L, LUA_REGISTRYINDEX);
}

/* Prepares environment for the target to be called; sets up state
   (and returns it), stores target to be invoked to the state. */
static lua_State *
callback_prepare_call (Callback *callback)
{
  /* Get access to proper Lua context. */
  lua_State *L = callback->L;
  lua_rawgeti (L, LUA_REGISTRYINDEX, callback->thread_ref);
  if (lua_isthread (L, -1))
    {
      L = lua_tothread (L, -1);
      if (lua_status (L) != 0)
	{
	  /* Thread is not in usable state for us, it is suspended, we
	     cannot afford to resume it, because it is possible that
	     the routine we are about to call is actually going to
	     resume it.  Create new thread instead and switch closure
	     to its context. */
	  L = lua_newthread (L);
	  luaL_unref (L, LUA_REGISTRYINDEX, callback->thread_ref);
	  callback->thread_ref = luaL_ref (callback->L, LUA_REGISTRYINDEX);
	}
    }
  lua_pop (callback->L, 1);
  lua_rawgeti (L, LUA_REGISTRYINDEX, callback->target_ref);
  return callback->L = L;
}

/* Frees everything allocated in Callback. */
static void
callback_destroy (Callback *callback)
{
  luaL_unref (callback->L, LUA_REGISTRYINDEX, callback->target_ref);
  luaL_unref (callback->L, LUA_REGISTRYINDEX, callback->thread_ref);
}

/* Closure callback, called by libffi when C code wants to invoke Lua
   callback. */
static void
closure_callback (ffi_cif *cif, void *ret, void **args, void *closure_arg)
{
  Callable *callable;
  FfiClosure *closure = closure_arg;
  gint res, npos, i, stacktop;
  Param *param;

  /* Get access to proper Lua context. */
  lua_State *L = callback_prepare_call (&closure->callback);

  /* Get access to Callable structure. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, closure->callable_ref);
  callable = lua_touserdata (L, -1);
  lua_pop (L, 1);

  /* Remember stacktop, this is the position on which we should expect
     return values (note that callback_prepare_call already pushed
     function to be executed to the stack). */
  stacktop = lua_gettop (L) - 1;

  /* Marshall 'self' argument, if it is present. */
  npos = 0;
  if (callable->has_self)
    {
      if (lgi_compound_create (L, g_base_info_get_container(callable->info),
			       ((GIArgument*) args[0])->v_pointer, FALSE, 0))
	npos++;
    }

  /* Marshal input arguments to lua. */
  param = callable->params;
  for (i = 0; i < callable->nargs; ++i, ++param)
    if (!param->internal && param->dir != GI_DIRECTION_OUT)
      {
	lgi_marshal_arg_2lua (L, &param->ti, GI_TRANSFER_NOTHING,
			      (GIArgument *) args[i + callable->has_self],
			      0, FALSE, callable->info,
			      args + callable->has_self);
	npos++;
      }

  /* Call it. */
  res = lua_pcall (L, npos, LUA_MULTRET, 0);
  npos = stacktop;

  /* Check, whether we can report an error here. */
  if (res == 0)
    {
      /* Marshal return value from Lua. */
      int to_pop;
      if (g_type_info_get_tag (&callable->retval.ti) != GI_TYPE_TAG_VOID)
	{
	  to_pop = lgi_marshal_arg_2c (L, &callable->retval.ti, NULL,
				       callable->retval.transfer, ret, npos,
				       FALSE, callable->info,
				       args + callable->has_self);
	  if (to_pop != 0)
	    {
	      g_warning ("cbk `%s.%s': return (transfer none) %d, unsafe!",
			 g_base_info_get_namespace (callable->info),
			 g_base_info_get_name (callable->info), to_pop);
	      lua_pop (L, to_pop);
	    }

	  npos++;
	}

      /* Marshal output arguments from Lua. */
      param = callable->params;
      for (i = 0; i < callable->nargs; ++i, ++param)
	if (!param->internal && param->dir != GI_DIRECTION_IN)
	  {
	    to_pop =
	      lgi_marshal_arg_2c (L, &param->ti, &param->ai, param->transfer,
				  (GIArgument *)args[i + callable->has_self],
				  npos, FALSE, callable->info,
				  args + callable->has_self);
	    if (to_pop != 0)
	      {
		g_warning ("cbk %s.%s: arg `%s' (transfer none) %d, unsafe!",
			   g_base_info_get_namespace (callable->info),
			   g_base_info_get_name (callable->info),
			   g_base_info_get_name (&param->ai), to_pop);
		lua_pop (L, to_pop);
	      }

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
  else
    g_warning ("ignoring error from closure: %s", lua_tostring (L, -1));

  /* If the closure is marked as autodestroy, destroy it now.  Note that it is
     unfortunately not possible to destroy it directly here, because we would
     delete the code under our feet and crash and burn :-(. Instead, we create
     marshal guard and leave it to GC to destroy the closure later. */
  if (closure->autodestroy)
    {
      lgi_closure_guard (L, closure);
      lua_pop (L, 1);
    }

  /* This is NOT called by Lua, so we better leave the Lua stack we
     used pretty much tidied. */
  lua_settop (L, stacktop);
}

/* Destroys specified closure. */
void
lgi_closure_destroy (gpointer user_data)
{
  FfiClosure* closure = user_data;

  luaL_unref (closure->callback.L, LUA_REGISTRYINDEX, closure->callable_ref);
  callback_destroy (&closure->callback);
  ffi_closure_free (closure);
}

/* Creates closure from Lua function to be passed to C. */
gpointer
lgi_closure_create (lua_State *L, GICallableInfo *ci, int target,
		    gboolean autodestroy, gpointer *call_addr)
{
  FfiClosure *closure;
  Callable *callable;

  /* Prepare callable and store reference to it. */
  lgi_callable_create (L, ci);
  callable = lua_touserdata (L, -1);

  /* Allocate closure space. */
  closure = ffi_closure_alloc (sizeof (FfiClosure), call_addr);
  closure->callable_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Initialize closure callback target. */
  callback_create (L, &closure->callback, target);

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

  return closure;
}

static int
closureguard_gc(lua_State *L)
{
  gpointer closure = *(gpointer *) lua_touserdata (L, 1);
  lgi_closure_destroy (closure);
  return 0;
}

#define UD_CLOSUREGUARD "lgi.closureguard"
static const struct luaL_reg closureguard_reg[] = {
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
  luaL_getmetatable (L, UD_CLOSUREGUARD);
  lua_setmetatable (L, -2);
}

typedef struct _GlibClosure
{
  GClosure closure;

  /* Target callback of the closure. */
  Callback callback;
} GlibClosure;

static void
lgi_gclosure_finalize (gpointer notify_data, GClosure *closure)
{
  GlibClosure *c = (GlibClosure *) closure;
  callback_destroy (&c->callback);
}

static void
lgi_gclosure_marshal (GClosure *closure, GValue *return_value,
		      guint n_param_values, const GValue *param_values,
		      gpointer invocation_hint, gpointer marshal_data)
{
  GlibClosure *c = (GlibClosure *) closure;
  int vals = 0, res;

  /* Prepare context in which will everything happen. */
  lua_State *L = callback_prepare_call (&c->callback);
  luaL_checkstack (L, n_param_values + 1, "");

  /* Push parameters. */
  while (n_param_values--)
    {
      lgi_marshal_val_2lua (L, NULL, GI_TRANSFER_NOTHING, param_values++);
      vals++;
    }

  /* Invoke the function. */
  res = lua_pcall (L, vals, 1, 0);
  if (res == 0)
    lgi_marshal_val_2c (L, NULL, GI_TRANSFER_NOTHING, return_value, -1);
}

GClosure *
lgi_gclosure_create (lua_State *L, int target)
{
  GlibClosure *c;
  int type = lua_type (L, target);

  /* Check that target is something we can call. */
  if (type != LUA_TFUNCTION && type != LUA_TTABLE && type != LUA_TUSERDATA)
    {
	luaL_typerror (L, target, lua_typename (L, LUA_TFUNCTION));
      return NULL;
    }

  /* Create new closure instance. */
  c = (GlibClosure *) g_closure_new_simple (sizeof (GlibClosure), NULL);

  /* Initialize callback target. */
  callback_create (L, &c->callback, target);

  /* Set marshaller for the closure. */
  g_closure_set_marshal (&c->closure, lgi_gclosure_marshal);

  /* Add destruction notifier. */
  g_closure_add_finalize_notifier (&c->closure, NULL, lgi_gclosure_finalize);

  /* Remove floating ref from the closure, it is useless for us. */
  g_closure_ref (&c->closure);
  g_closure_sink (&c->closure);
  return &c->closure;
}

void
lgi_callable_init (lua_State *L)
{
  /* Register callable metatable. */
  luaL_newmetatable (L, UD_CALLABLE);
  luaL_register (L, NULL, callable_reg);
  lua_pop (L, 1);

  /* Register closureguard metatable. */
  luaL_newmetatable (L, UD_CLOSUREGUARD);
  luaL_register (L, NULL, closureguard_reg);
  lua_pop (L, 1);
}
