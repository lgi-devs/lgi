/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011, 2012, 2013 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * This code deals with calling from Lua to C and vice versa, using
 * gobject-introspection information and libffi machinery.
 */

#include "lgi.h"
#include <string.h>
#include <ffi.h>

/* Kinds or Param structure variation. */
typedef enum _ParamKind
  {
    /* Ordinary typeinfo (ti)-based parameter. */
    PARAM_KIND_TI = 0,

    /* Foreign record. ti is unused. */
    PARAM_KIND_RECORD,

    /* Foreign enum/flags. ti contains underlying numeric type. */
    PARAM_KIND_ENUM
  } ParamKind;

/* Represents single parameter in callable description. */
typedef struct _Param
{
  GITypeInfo *ti;
  GIArgInfo ai;

  /* Indicates whether ai field is valid. */
  guint has_arg_info : 1;

  /* Direction of the argument. */
  guint dir : 2;

  /* Ownership passing rule for output parameters. */
  guint transfer : 2;

  /* Flag indicating whether this parameter is represented by Lua input and/or
     returned value.  Not represented are e.g. callback's user_data, array
     sizes etc. */
  guint internal : 1;

  /* Flag indicating that this is internal user_data value for the
     callback.  This parameter is supplied automatically, not
     explicitely from Lua. */
  guint internal_user_data : 1;

  /* Set to nonzero if this argument is user_data for closure which is
     marked as (scope call). */
  guint call_scoped_user_data : 1;

  /* Number of closures bound to this argument.  0 if this is not
     user_data for closure. */
  guint n_closures : 4;

  /* Type of the argument, one of ParamKind values. */
  guint kind : 2;

  /* Index into env table attached to the callable, contains repotype
     table for specified argument. */
  guint repotype_index : 4;
} Param;

/* Structure representing userdata allocated for any callable, i.e. function,
   method, signal, vtable, callback... */
typedef struct _Callable
{
  /* Stored callable info. */
  GICallableInfo *info;

  /* Address of the function. */
  gpointer address;

  /* Optional, associated 'user_data' context field. */
  gpointer user_data;

  /* Flags with function characteristics. */
  guint has_self : 1;
  guint throws : 1;
  guint nargs : 6;
  guint ignore_retval : 1;
  guint is_closure_marshal : 1;

  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* Param return value and pointer to nargs Param instances. */
  Param retval;
  Param *params;

  /* ffi_type* array here, contains ffi_type[nargs + 2] entries. */
  /* params points here, contains Param[nargs] entries. */
} Callable;

/* Address is lightuserdata of Callable metatable in Lua registry. */
static int callable_mt;

/* Structure containing basic callback information. */
typedef struct _Callback
{
  /* Thread which created callback and Lua-reference to it (so that it
     is not GCed). */
  lua_State *L;
  int thread_ref;

  /* State lock, to be passed to lgi_state_enter() when callback is
     invoked. */
  gpointer state_lock;
} Callback;

typedef struct _FfiClosureBlock FfiClosureBlock;

/* Single element in FFI callbacks block. */
typedef struct _FfiClosure
{
  /* Libffi closure object. */
  ffi_closure ffi_closure;

  /* Pointer to the block to which this closure belongs. */
  FfiClosureBlock *block;

  union
  {
    struct
    {
      /* Lua reference to associated Callable. */
      int callable_ref;

      /* Callable's target to be invoked (either function,
	 userdata/table with __call metafunction or coroutine (which
	 is resumed instead of called). */
      int target_ref;
    };

    /* Closure's entry point, stored only temporarily until closure is
       created. */
    gpointer call_addr;
  };

  /* Flag indicating whether closure should auto-destroy itself after it is
     called. */
  guint autodestroy : 1;

  /* Flag indicating whether the closure was already created. */
  guint created : 1;
} FfiClosure;

/* Structure containing closure block. This is user_data block for
   C-side closure arguments. */
struct _FfiClosureBlock
{
  /* 1st closure. */
  FfiClosure ffi_closure;

  /* Target to be invoked. */
  Callback callback;

  /* Number of other closures in the block, excluding the forst one
     contained already in this header. */
  int closures_count;

  /* Variable-length array of pointers to other closures.
     Unfortunately libffi does not allow to allocate contiguous block
     containing more closures, otherwise this array would simply
     contain FfiClosure instances instead of pointers to dynamically
     allocated ones. */
  FfiClosure *ffi_closures[1];
};

/* lightuserdata key to callable cache table. */
static int callable_cache;

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
  switch (param->kind)
    {
    case PARAM_KIND_RECORD:
      return &ffi_type_pointer;

    case PARAM_KIND_ENUM:
      return param->ti ? get_simple_ffi_type (g_type_info_get_tag (param->ti))
	: &ffi_type_sint;

    case PARAM_KIND_TI:
      break;
    }

  /* In case of inout or out parameters, the type is always pointer. */
  GITypeTag tag = g_type_info_get_tag (param->ti);
  ffi_type* ffi = g_type_info_is_pointer(param->ti)
    ? &ffi_type_pointer : get_simple_ffi_type (tag);
  if (ffi == NULL)
    {
      /* Something more complex. */
      if (tag == GI_TYPE_TAG_INTERFACE)
	{
	  GIBaseInfo *ii = g_type_info_get_interface (param->ti);
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

static void
callable_param_init (Param *param)
{
  param->ti = NULL;
  param->has_arg_info =FALSE;
  param->internal = FALSE;
  param->internal_user_data = FALSE;
  param->n_closures = 0;
  param->call_scoped_user_data = FALSE;
  param->kind = PARAM_KIND_TI;
  param->repotype_index = 0;
}

static Callable *
callable_allocate (lua_State *L, int nargs, ffi_type ***ffi_args)
{
  int argi;

  /* Create userdata structure. */
  luaL_checkstack (L, 2, NULL);
  Callable *callable = lua_newuserdata (L, sizeof (Callable) +
					sizeof (ffi_type) * (nargs + 2) +
					sizeof (Param) * nargs);
  lua_pushlightuserdata (L, &callable_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);

  /* Inititialize callable contents. */
  *ffi_args = (ffi_type **) &callable[1];
  callable->params = (Param *) &(*ffi_args)[nargs + 2];
  callable->nargs = nargs;
  callable->user_data = NULL;
  callable->info = NULL;
  callable->has_self = 0;
  callable->throws = 0;
  callable->ignore_retval = 0;
  callable->is_closure_marshal = 0;

  /* Clear all 'internal' flags inside callable parameters, parameters are then
     marked as internal during processing of their parents. */
  callable_param_init (&callable->retval);
  for (argi = 0; argi < nargs; argi++)
    callable_param_init (&callable->params[argi]);

  return callable;
}

int
lgi_callable_create (lua_State *L, GICallableInfo *info, gpointer addr)
{
  Callable *callable;
  Param *param;
  ffi_type **ffi_arg, **ffi_args;
  ffi_type *ffi_retval;
  gint nargs, argi, arg;

  /* Allocate Callable userdata. */
  nargs = g_callable_info_get_n_args (info);
  callable = callable_allocate (L, nargs, &ffi_args);
  callable->info = g_base_info_ref (info);
  callable->address = addr;
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

  /* Process return value. */
  callable->retval.ti = g_callable_info_get_return_type (callable->info);
  callable->retval.dir = GI_DIRECTION_OUT;
  callable->retval.transfer = g_callable_info_get_caller_owns (callable->info);
  callable->retval.internal = FALSE;
  callable->retval.repotype_index = 0;
  ffi_retval = get_ffi_type (&callable->retval);
  callable_mark_array_length (callable, callable->retval.ti);

  /* Process 'self' argument, if present. */
  ffi_arg = &ffi_args[0];
  if (callable->has_self)
    *ffi_arg++ = &ffi_type_pointer;

  /* Process the rest of the arguments. */
  param = &callable->params[0];
  for (argi = 0; argi < nargs; argi++, param++, ffi_arg++)
    {
      g_callable_info_load_arg (callable->info, argi, &param->ai);
      param->has_arg_info = TRUE;
      param->ti = g_arg_info_get_type (&param->ai);
      param->dir = g_arg_info_get_direction (&param->ai);
      param->transfer = g_arg_info_get_ownership_transfer (&param->ai);
      *ffi_arg = (param->dir == GI_DIRECTION_IN)
	? get_ffi_type (param) : &ffi_type_pointer;

      /* Mark closure-related user_data fields and possibly destroy_notify
	 fields as internal. */
      arg = g_arg_info_get_closure (&param->ai);
      if (arg >= 0 && arg < nargs)
	{
	  callable->params[arg].internal = TRUE;
	  if (arg == argi)
	    callable->params[arg].internal_user_data = TRUE;
	  callable->params[arg].n_closures++;
	  if (g_arg_info_get_scope (&param->ai) == GI_SCOPE_TYPE_CALL)
	    callable->params[arg].call_scoped_user_data = TRUE;
	}
      arg = g_arg_info_get_destroy (&param->ai);
      if (arg > 0 && arg < nargs)
	callable->params[arg].internal = TRUE;

      /* Similarly for array length field. */
      callable_mark_array_length (callable, param->ti);

      /* In case that we have an out or inout argument and callable
	 returns boolean, mark it as ignore_retval (because we will
	 signalize failure by returning nil instead of extra
	 value). */
      if (param->dir != GI_DIRECTION_IN
	  && g_type_info_get_tag (callable->retval.ti) == GI_TYPE_TAG_BOOLEAN)
	callable->ignore_retval = 1;
    }

  /* Manual adjustment of 'GObject.ClosureMarshal' type, which is
     crucial for lgi but is missing an array annotation in
     glib/gobject-introspection < 1.30. */
  if (!GLIB_CHECK_VERSION (2, 30, 0)
      && !strcmp (g_base_info_get_namespace (info), "GObject")
      && !strcmp (g_base_info_get_name (info), "ClosureMarshal"))
    {
      callable->is_closure_marshal = 1;
      callable->params[2].internal = 1;
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

  return 1;
}

static int
callable_param_get_kind (lua_State *L)
{
  int kind = -1, top = lua_gettop (L);
  if (lgi_udata_test (L, -1, LGI_GI_INFO))
    kind = PARAM_KIND_TI;
  else
    {
      luaL_checktype (L, -1, LUA_TTABLE);
      lua_getmetatable (L, -1);
      if (!lua_isnil (L, -1))
	{
	  lua_getfield (L, -1, "_type");
	  if (!lua_isnil (L, -1))
	    {
	      const char *type = lua_tostring (L, -1);
	      if (g_strcmp0 (type, "struct") == 0
		  || g_strcmp0 (type, "union") == 0)
		kind = PARAM_KIND_RECORD;
	      else if (g_strcmp0 (type, "enum") == 0
		       || g_strcmp0 (type, "flags") == 0)
		kind = PARAM_KIND_ENUM;
	    }
	}
    }

  lua_settop (L, top);
  return kind;
}

static const char *dirs[] = { "in", "out", "inout", NULL };

/* Parses single 'Param' structure from the table on the top of the
   stack.  Pops the table from the stack. */
static void
callable_param_parse (lua_State *L, Param *param)
{
  int kind = callable_param_get_kind (L);

  /* Initialize parameters to default values. */
  param->transfer = GI_TRANSFER_NOTHING;
  param->ti = NULL;
  if (kind == -1)
    {
      /* Check the direction. */
      lua_getfield (L, -1, "dir");
      if (!lua_isnil (L, -1))
	param->dir = luaL_checkoption (L, -1, dirs[0], dirs);
      lua_pop (L, 1);

      /* Get transfer flag, prepare default according to dir. */
      lua_getfield (L, -1, "xfer");
      param->transfer = lua_toboolean (L, -1)
	? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING;
      lua_pop (L, 1);

      /* Get type, assume record (if not overriden by real giinfo type
	 below). */
      lua_getfield (L, -1, "type");
      if (!lua_isnil (L, -1))
	{
	  /* This is actually an enum, and 'type' field contains
	     numeric type for this enum.  Store it into the ti. */
	  GITypeInfo **ti = luaL_checkudata (L, -1, LGI_GI_INFO);
	  param->ti = g_base_info_ref (*ti);
	}
      lua_pop (L, 1);

      /* Finally get the type from the table (from index 1) and
	 replace the table with the type. */
      lua_rawgeti (L, -1, 1);
      lua_replace (L, -2);
    }

  /* Parse the type. */
  if (kind == -1)
    kind = callable_param_get_kind (L);
  if (kind == PARAM_KIND_TI)
    {
      /* Expect typeinfo. */
      GITypeInfo **pti = lua_touserdata (L, -1);
      param->ti = g_base_info_ref (*pti);
      param->kind = kind;
      lua_pop (L, 1);
    }
  else if (kind == PARAM_KIND_ENUM || kind == PARAM_KIND_RECORD)
    {
      /* Add it to the env table. */
      int index = lua_objlen (L, -2) + 1;
      lua_rawseti (L, -2, index);
      param->repotype_index = index;
      param->kind = kind;
    }
  else
    luaL_error (L, "bad efn def");
}

/* Parses callable from given table. */
int
lgi_callable_parse (lua_State *L, int info, gpointer addr)
{
  Callable *callable;
  int nargs, i;
  ffi_type **ffi_args;
  ffi_type *ffi_retval;

  /* Allocate the raw structure. */
  nargs = lua_objlen (L, info);
  callable = callable_allocate (L, nargs, &ffi_args);

  /* Create 'env' table. */
  lua_newtable (L);

  /* Add function name to it. */
  lua_getfield (L, info, "name");
  lua_rawseti (L, -2, 0);

  /* Get address of the function. */
  if (addr == NULL)
    {
      lua_getfield (L, info, "addr");
      addr = lua_touserdata (L, -1);
      lua_pop (L, 1);
    }
  callable->address = addr;

  /* Handle 'return' table. */
  lua_getfield (L, info, "ret");

  /* Get ignore_retval flag. */
  lua_getfield (L, -1, "phantom");
  callable->ignore_retval = lua_toboolean (L, -1);
  lua_pop (L, 1);

  /* Parse return value param. */
  callable->retval.dir = GI_DIRECTION_OUT;
  callable_param_parse (L, &callable->retval);
  ffi_retval = get_ffi_type (&callable->retval);

  /* Parse individual arguments. */
  for (i = 0; i < nargs; i++)
    {
      lua_rawgeti (L, info, i + 1);
      callable->params[i].dir = GI_DIRECTION_IN;
      callable_param_parse (L, &callable->params[i]);
      ffi_args[i] = (callable->params[i].dir == GI_DIRECTION_IN)
	? get_ffi_type (&callable->params[i]) : &ffi_type_pointer;
    }

  /* Handle 'throws' flag. */
  lua_getfield (L, info, "throws");
  callable->throws = lua_toboolean (L, -1);
  lua_pop (L, 1);
  if (callable->throws)
    ffi_args[i] = &ffi_type_pointer;

  /* Create ffi_cif. */
  if (ffi_prep_cif (&callable->cif, FFI_DEFAULT_ABI,
		    nargs + callable->throws,
		    ffi_retval, ffi_args) != FFI_OK)
    return luaL_error (L, "ffi_prep_cif failed for parsed");

  /* Attach env table to the returned callable instance. */
  lua_setfenv (L, -2);
  return 1;
}

/* Checks whether given argument is Callable userdata. */
static Callable *
callable_get (lua_State *L, int narg)
{
  luaL_checkstack (L, 3, "");
  if (lua_getmetatable (L, narg))
    {
      lua_pushlightuserdata (L, &callable_mt);
      lua_rawget (L, LUA_REGISTRYINDEX);
      if (lua_rawequal (L, -1, -2))
	{
	  lua_pop (L, 2);
	  return lua_touserdata (L, narg);
	}
    }

  lua_pushfstring (L, "expected lgi.callable, got %s",
		   lua_typename (L, lua_type (L, narg)));
  luaL_argerror (L, narg, lua_tostring (L, -1));
  return NULL;
}

static void
callable_param_destroy (Param *param)
{
  if (param->ti)
    g_base_info_unref (param->ti);
}

static int
callable_gc (lua_State *L)
{
  int i;

  /* Unref embedded 'info' field. */
  Callable *callable = callable_get (L, 1);
  if (callable->info)
    g_base_info_unref (callable->info);

  /* Destroy all params. */
  for (i = 0; i < callable->nargs; i++)
    callable_param_destroy (&callable->params[i]);

  callable_param_destroy (&callable->retval );
  return 0;
}

static void
callable_describe (lua_State *L, Callable *callable, FfiClosure *closure)
{
  luaL_checkstack (L, 2, "");

  if (closure == NULL)
    lua_pushfstring (L, "%p", callable->address);
  else
    {
      gconstpointer ptr;
      lua_rawgeti (L, LUA_REGISTRYINDEX, closure->target_ref);
      ptr = lua_topointer (L, -1);
      if (ptr != NULL)
	lua_pushfstring (L, "%s: %p", luaL_typename (L, -1),
			 lua_topointer (L, -1));
      else
	lua_pushstring (L, luaL_typename (L, -1));
      lua_replace (L, -2);
    }

  if (callable->info)
    {
      lua_pushfstring (L, "lgi.%s (%s): ",
		       (GI_IS_FUNCTION_INFO (callable->info) ? "fun" :
			(GI_IS_SIGNAL_INFO (callable->info) ? "sig" :
			 (GI_IS_VFUNC_INFO (callable->info) ? "vfn" : "cbk"))),
		       lua_tostring (L, -1));
      lua_concat (L, lgi_type_get_name (L, callable->info) + 1);
    }
  else
    {
      lua_getfenv (L, 1);
      lua_rawgeti (L, -1, 0);
      lua_replace (L, -2);
      lua_pushfstring (L, "lgi.efn (%s): %s", lua_tostring (L, -2),
		       lua_tostring (L, -1));
      lua_replace (L, -2);
    }

  lua_replace (L, -2);
}

static int
callable_tostring (lua_State *L)
{
  Callable *callable = callable_get (L, 1);

  callable_describe (L, callable, NULL);
  return 1;
}

static int
callable_param_2c (lua_State *L, Param *param, int narg, int parent,
		   GIArgument *arg, int callable_index,
		   Callable *callable, void **args)
{
  int nret = 0;
  if (param->kind == PARAM_KIND_ENUM && lua_type (L, narg) != LUA_TNUMBER)
    {
      /* Convert enum symbolic value to numeric one. */
      lua_getfenv (L, callable_index);
      lua_rawgeti (L, -1, param->repotype_index);
      lua_pushvalue (L, narg);
      lua_call (L, 1, 1);
      narg = -1;
    }

  if (param->kind != PARAM_KIND_RECORD)
    {
      if (param->ti)
	nret = lgi_marshal_2c (L, param->ti,
			       param->has_arg_info ? &param->ai : NULL,
			       param->transfer, arg, narg, parent,
			       callable->info, args + callable->has_self);
      else
	{
	  union { GIArgument arg; int i; } *u = (gpointer) arg;
	  u->i = lua_tonumber (L, narg);
	}

      /* Stack cleanup from enum value conversion. */
      if (narg == -1)
	lua_pop (L, 2);
    }
  else
    {
      /* Marshal record according to custom information. */
      lua_getfenv (L, callable_index);
      lua_rawgeti (L, -1, param->repotype_index);
      lgi_record_2c (L, narg, &arg->v_pointer, FALSE,
		     param->transfer != GI_TRANSFER_NOTHING, TRUE, FALSE);
      lua_pop (L, 1);
    }

  return nret;
}

static void
callable_param_2lua (lua_State *L, Param *param, GIArgument *arg,
		     int parent, int callable_index,
		     Callable *callable, void **args)
{
  if (param->kind != PARAM_KIND_RECORD)
    {
      if (param->ti)
	lgi_marshal_2lua (L, param->ti, callable->info ? &param->ai : NULL,
			  param->dir, param->transfer,
			  arg, parent, callable->info,
			  args + callable->has_self);
      else
	{
	  union { GIArgument arg; int i; } *u = (gpointer) arg;
	  lua_pushnumber (L, u->i);
	}
    }

  if (param->kind == PARAM_KIND_TI)
    return;

  lua_getfenv (L, callable_index);
  lua_rawgeti (L, -1, param->repotype_index);
  if (param->kind == PARAM_KIND_RECORD)
    {
      /* Marshal record according to custom information. */
      lgi_record_2lua (L, arg->v_pointer,
		       param->transfer != GI_TRANSFER_NOTHING, parent);
      lua_remove (L, -2);
    }
  else
    {
      /* Convert enum numeric value to symbolic one. */
      lua_pushvalue (L, -3);
      lua_gettable (L, -2);
      lua_replace (L, -4);
      lua_pop (L, 2);
    }
}

static int
callable_call (lua_State *L)
{
  Param *param;
  int i, lua_argi, nret, caller_allocated = 0, nargs;
  GIArgument retval, *args;
  void **ffi_args, **redirect_out;
  GError *err = NULL;
  gpointer state_lock = lgi_state_get_lock (L);
  Callable *callable = callable_get (L, 1);

  /* Make sure that all unspecified arguments are set as nil; during
     marshalling we might create temporary values on the stack, which
     can be confused with input arguments expected but not passed by
     caller. */
  lua_settop(L, callable->has_self + callable->nargs + 1);

  /* We cannot push more stuff than count of arguments we have. */
  luaL_checkstack (L, callable->nargs, "");

  /* Prepare data for the call. */
  nargs = callable->nargs + callable->has_self;
  args = g_newa (GIArgument, nargs);
  redirect_out = g_newa (void *, nargs + callable->throws);
  ffi_args = g_newa (void *, nargs + callable->throws);

  /* Prepare 'self', if present. */
  lua_argi = 2;
  nret = 0;
  if (callable->has_self)
    {
      GIBaseInfo *parent = g_base_info_get_container (callable->info);
      GIInfoType type = g_base_info_get_type (parent);
      if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_INTERFACE)
	{
	  args[0].v_pointer =
	    lgi_object_2c (L, 2, g_registered_type_info_get_g_type (parent),
			   FALSE, FALSE, FALSE);
	  nret++;
	}
      else
	{
	  lgi_type_get_repotype (L, G_TYPE_INVALID, parent);
	  lgi_record_2c (L, 2, &args[0].v_pointer, FALSE, FALSE, FALSE, FALSE);
	  nret++;
	}

      ffi_args[0] = &args[0];
      lua_argi++;
    }

  /* Prepare proper call->ffi_args[] pointing to real args (or
     redirects in case of inout/out parameters). Note that this loop
     cannot be merged with following marshalling loop, because during
     marshalling of closure or arrays marshalling code can read/write
     values ahead of currently marshalled value. */
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

      if (param->n_closures > 0)
	{
	  args[argi].v_pointer = lgi_closure_allocate (L, param->n_closures);
	  if (param->call_scoped_user_data)
	    /* Add guard which releases closure block after the
	       call. */
	    *lgi_guard_create (L, lgi_closure_destroy) = args[argi].v_pointer;
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
	  nret += callable_param_2c (L, param, lua_argi++, 0, &args[argi],
				     1, callable, ffi_args);
	/* Special handling for out/caller-alloc structures; we have to
	   manually pre-create them and store them on the stack. */
	else if (callable->info && g_arg_info_is_caller_allocates (&param->ai)
		 && lgi_marshal_2c_caller_alloc (L, param->ti, &args[argi], 0))
	  {
	    /* Even when marked as OUT, caller-allocates arguments
	       behave as if they are actually IN from libffi POV. */
	    ffi_args[argi] = &args[argi];

	    /* Move the value on the stack *below* any already present
	       temporary values. */
	    lua_insert (L, -nret - 1);
	    caller_allocated++;
	  }
	else
	  /* Normal OUT parameters.  Ideally we don't have to touch
	     them, but see https://github.com/pavouk/lgi/issues/118 */
	  memset (&args[argi], 0, sizeof (args[argi]));
      }
    else if (param->internal_user_data)
      /* Provide userdata for the callback. */
      args[i + callable->has_self].v_pointer = callable->user_data;

  /* Add error for 'throws' type function. */
  if (callable->throws)
    {
      redirect_out[nargs] = &err;
      ffi_args[nargs] = &redirect_out[nargs];
    }

  /* Unlock the state. */
  lgi_state_leave (state_lock);

  /* Call the function. */
  ffi_call (&callable->cif, callable->address, &retval, ffi_args);

  /* Heading back to Lua, lock the state back again. */
  lgi_state_enter (state_lock);

  /* Pop any temporary items from the stack which might be stored there by
     marshalling code. */
  lua_pop (L, nret);

  /* Handle return value. */
  nret = 0;
  if (!callable->ignore_retval
      && (callable->retval.ti == NULL
	  || (g_type_info_get_tag (callable->retval.ti) != GI_TYPE_TAG_VOID
	      || g_type_info_is_pointer (callable->retval.ti))))
    {
      callable_param_2lua (L, &callable->retval, &retval, LGI_PARENT_IS_RETVAL,
			   1, callable, ffi_args);
      nret++;
      lua_insert (L, -caller_allocated - 1);
    }
  else if (callable->ignore_retval)
    {
      /* Make sure that returned boolean is converted according to
	 ffi_call rules. */
      union {
	GIArgument arg;
	ffi_sarg s;
      } *ru = (gpointer) &retval;
      ru->arg.v_boolean = (gboolean) ru->s;
    }

  /* Check, whether function threw. */
  if (err != NULL)
    {
      if (nret == 0)
	{
	  lua_pushboolean (L, 0);
	  nret = 1;
	}

      /* Wrap error instance into GLib.Error record. */
      lgi_type_get_repotype (L, G_TYPE_ERROR, NULL);
      lgi_record_2lua (L, err, TRUE, 0);
      return nret + 1;
    }

  /* Process output parameters. */
  param = &callable->params[0];
  for (i = 0; i < callable->nargs; i++, param++)
    if (!param->internal && param->dir != GI_DIRECTION_IN)
      {
	if (callable->info && g_arg_info_is_caller_allocates (&param->ai)
	    && lgi_marshal_2c_caller_alloc (L, param->ti, NULL,
					    -caller_allocated  - nret))
	  /* Caller allocated parameter is already marshalled and
	     lying on the stack. */
	  caller_allocated--;
	else
	  {
	    /* Marshal output parameter. */
	    callable_param_2lua (L, param, &args[i + callable->has_self],
				 0, 1, callable, ffi_args);
	    lua_insert (L, -caller_allocated - 1);
	  }

	/* In case that this callable is in ignore-retval mode and
	   function actually returned FALSE, replace the already
	   marshalled return value with NULL. */
	if (callable->ignore_retval && !retval.v_boolean)
	  {
	    lua_pushnil (L);
	    lua_replace (L, -caller_allocated - 2);
	  }

	nret++;
      }

  /* When function can throw and we are not returning anything, be
     sure to return at least 'true', so that caller can check for
     error in a usual way (i.e. by Lua's assert() call). */
  if (nret == 0 && callable->throws)
    {
      lua_pushboolean (L, 1);
      nret = 1;
    }

  g_assert (caller_allocated == 0);
  return nret;
}

static int
callable_index (lua_State *L)
{
  Callable *callable = callable_get (L, 1);
  const gchar *verb = lua_tostring (L, 2);
  if (g_strcmp0 (verb, "info") == 0)
    return lgi_gi_info_new (L, g_base_info_ref (callable->info));
  else if (g_strcmp0 (verb, "params") == 0)
    {
      int index = 1, i;
      Param *param;

      lua_newtable (L);
      if (callable->has_self)
	{
	  lua_newtable (L);
	  lua_pushboolean (L, 1);
	  lua_setfield (L, -2, "in");
	  lua_rawseti (L, -2, index++);
	}
      for (i = 0, param = callable->params; i < callable->nargs; i++, param++)
	if (!param->internal)
	  {
	    lua_newtable (L);
	    /* Add name. */
	    if (param->has_arg_info)
	      {
		lua_pushstring (L, g_base_info_get_name (&param->ai));
		lua_setfield (L, -2, "name");
	      }

	    /* Add typeinfo. */
	    if (param->ti)
	      {
		lgi_gi_info_new (L, g_base_info_ref (param->ti));
		lua_setfield (L, -2, "typeinfo");
	      }

	    /* Add in.out info. */
	    if (param->dir == GI_DIRECTION_IN ||
		param->dir == GI_DIRECTION_INOUT)
	      {
		lua_pushboolean (L, 1);
		lua_setfield (L, -2, "in");
	      }
	    if (param->dir == GI_DIRECTION_OUT ||
		param->dir == GI_DIRECTION_INOUT)
	      {
		lua_pushboolean (L, 1);
		lua_setfield (L, -2, "out");
	      }
	    lua_rawseti (L, -2, index++);
	  }
      return 1;
    }
  else if (g_strcmp0 (verb, "user_data") == 0)
    {
      lua_pushlightuserdata (L, callable->user_data);
      return 1;
    }

  return 0;
}

static int
callable_newindex (lua_State *L)
{
  Callable *callable = callable_get (L, 1);
  if (g_strcmp0 (lua_tostring (L, 2), "user_data") == 0)
    callable->user_data = lua_touserdata (L, 3);

  return 0;
}

static const struct luaL_Reg callable_reg[] = {
  { "__gc", callable_gc },
  { "__tostring", callable_tostring },
  { "__call", callable_call },
  { "__index", callable_index },
  { "__newindex", callable_newindex },
  { NULL, NULL }
};

/* Closure callback, called by libffi when C code wants to invoke Lua
   callback. */
static void
closure_callback (ffi_cif *cif, void *ret, void **args, void *closure_arg)
{
  Callable *callable;
  int callable_index;
  FfiClosure *closure = closure_arg;
  FfiClosureBlock *block = closure->block;
  gint res = 0, npos, i, stacktop;
  gboolean call;
  Param *param;
  (void)cif;

  /* Get access to proper Lua context. */
  lua_State *L = block->callback.L;
  lgi_state_enter (block->callback.state_lock);
  lua_rawgeti (L, LUA_REGISTRYINDEX, block->callback.thread_ref);
  L = lua_tothread (L, -1);
  call = (closure->target_ref != LUA_NOREF);
  if (call)
    {
      /* We will call target method, prepare context/thread to do
	 it. */
      if (lua_status (L) != 0)
	{
	  /* Thread is not in usable state for us, it is suspended, we
	     cannot afford to resume it, because it is possible that
	     the routine we are about to call is actually going to
	     resume it.  Create new thread instead and switch closure
	     to its context. */
	  L = lua_newthread (L);
	  lua_rawseti (L, LUA_REGISTRYINDEX, block->callback.thread_ref);
	}
      lua_pop (block->callback.L, 1);
      block->callback.L = L;

      /* Remember stacktop, this is the position on which we should
	 expect return values (note that callback_prepare_call already
	 might have pushed function to be executed to the stack). */
      stacktop = lua_gettop (L);

      /* Store function to be invoked to the stack. */
      lua_rawgeti (L, LUA_REGISTRYINDEX, closure->target_ref);
    }
  else
    {
      /* Cleanup the stack of the original thread. */
      lua_pop (block->callback.L, 1);
      stacktop = lua_gettop (L);
      if (lua_status (L) == 0)
	/* Thread is not suspended yet, so it contains initial
	   function at the top of the stack, so count with it. */
	stacktop--;
    }

  /* Get access to Callable structure. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, closure->callable_ref);
  callable = lua_touserdata (L, -1);
  callable_index = lua_gettop (L);

  /* Marshall 'self' argument, if it is present. */
  npos = 0;
  if (callable->has_self)
    {
      GIBaseInfo *parent = g_base_info_get_container (callable->info);
      GIInfoType type = g_base_info_get_type (parent);
      gpointer addr = ((GIArgument*) args[0])->v_pointer;
      npos++;
      if (type == GI_INFO_TYPE_OBJECT || type == GI_INFO_TYPE_INTERFACE)
	lgi_object_2lua (L, addr, FALSE, FALSE);
      else if (type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_UNION)
	{
	  lgi_type_get_repotype (L, G_TYPE_INVALID, parent);
	  lgi_record_2lua (L, addr, FALSE, 0);
	}
      else
	g_assert_not_reached ();
    }

  /* Marshal input arguments to lua. */
  param = callable->params;
  for (i = 0; i < callable->nargs; ++i, ++param)
    if (!param->internal && param->dir != GI_DIRECTION_OUT)
      {
	if G_LIKELY (i != 3 || !callable->is_closure_marshal)
	  {
	    GIArgument *real_arg = args[i + callable->has_self];
	    GIArgument arg_value;

	    if (param->dir == GI_DIRECTION_INOUT)
	      {
	        arg_value = *(GIArgument *) real_arg->v_pointer;
	        real_arg = &arg_value;
	      }

	    callable_param_2lua (L, param, real_arg, 0,
			         callable_index, callable,
			         args + callable->has_self);
	  }
	else
	  {
	    /* Workaround incorrectly annotated but crucial
	       ClosureMarshal callback.  Its 3rd argument is actually
	       an array of GValue, not a single GValue as missing
	       annotation suggests. */
	    guint i, nvals = ((GIArgument *)args[2])->v_uint32;
	    GValue* vals = ((GIArgument *)args[3])->v_pointer;
	    lua_createtable (L, nvals, 0);
	    for (i = 0; i < nvals; ++i)
	      {
		lua_pushnumber (L, i + 1);
		lgi_type_get_repotype (L, G_TYPE_VALUE, NULL);
		lgi_record_2lua (L, &vals[i], FALSE, 0);
		lua_settable (L, -3);
	      }
	  }
	npos++;
      }

  /* Remove callable userdata from callable_index, otehrwise they mess
     up carefully prepared stack structure. */
  lua_remove (L, callable_index);

  /* Call it. */
  if (call)
    {
      if (callable->throws)
        res = lua_pcall (L, npos, LUA_MULTRET, 0);
      else if (lua_pcall (L, npos, LUA_MULTRET, 0) != 0)
        {
          callable_describe (L, callable, closure);
          g_warning ("Error raised while calling '%s': %s",
                     lua_tostring (L, -1), lua_tostring (L, -2));
          lua_pop (L, 2);
        }
    }
  else
    {
#if LUA_VERSION_NUM >= 502
      res = lua_resume (L, NULL, npos);
#else
      res = lua_resume (L, npos);
#endif

      if (res == LUA_YIELD)
	/* For our purposes is YIELD the same as if the coro really
	   returned. */
	res = 0;
      else if (res == LUA_ERRRUN && !callable->throws)
	{
	  /* If closure is not allowed to return errors and coroutine
	     finished with error, rethrow the error in the context of
	     the original thread. */
	  lua_xmove (L, block->callback.L, 1);
	  lua_error (block->callback.L);
	}

      /* If coroutine somehow consumed more than expected(?), do not
	 blow up, adjust to the new situation. */
      if (stacktop > lua_gettop (L))
	stacktop = lua_gettop (L);
    }

  /* Reintroduce callable to the stack, we might need it during
     marshalling of the response. Put it right before all returns. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, closure->callable_ref);
  lua_insert (L, stacktop + 1);
  callable_index = stacktop + 1;
  npos = stacktop + 2;

  /* Check, whether we can report an error here. */
  if (res == 0)
    {
      int to_pop;
      GITypeTag tag;

      /* Make sure that all unspecified returns and outputs are set as
	 nil; during marshalling we might create temporary values on
	 the stack, which can be confused with output values expected
	 but not passed by caller. */
      lua_settop(L, lua_gettop (L) + callable->has_self + callable->nargs + 1);

      /* Marshal return value from Lua. */
      tag = g_type_info_get_tag (callable->retval.ti);
      if (tag != GI_TYPE_TAG_VOID
	  || g_type_info_is_pointer (callable->retval.ti))
	{
	  if (callable->ignore_retval)
	    /* Return value should be ignored on Lua side, so we have
	       to synthesize the return value for C side.  We should
	       return FALSE if next output argument is nil. */
	    *(ffi_sarg *) ret = lua_isnoneornil (L, npos) ? FALSE : TRUE;
	  else
	    {
	      to_pop = callable_param_2c (L, &callable->retval, npos,
					  LGI_PARENT_IS_RETVAL, ret,
					  callable_index, callable,
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
	}

      /* Marshal output arguments from Lua. */
      param = callable->params;
      for (i = 0; i < callable->nargs; ++i, ++param)
	if (!param->internal && param->dir != GI_DIRECTION_IN)
	  {
	    gpointer *arg = args[i + callable->has_self];
	    gboolean caller_alloc =
	      callable->info && g_arg_info_is_caller_allocates (&param->ai)
	      && g_type_info_get_tag (param->ti) == GI_TYPE_TAG_INTERFACE;
	    to_pop = callable_param_2c (L, param, npos, caller_alloc
					? LGI_PARENT_CALLER_ALLOC : 0, *arg,
					callable_index, callable,
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
  else
    {
      /* If the function is expected to return errors, create proper
	 error. */
      GError **err = ((GIArgument *) args[callable->has_self +
					  callable->nargs])->v_pointer;

      /* Check, whether thrown error is actually GLib.Error instance. */
      lgi_type_get_repotype (L, G_TYPE_ERROR, NULL);
      lgi_record_2c (L, -2, err, FALSE, TRUE, TRUE, TRUE);
      if (*err == NULL)
	{
	  /* Nope, so come up with something funny. */
	  GQuark q = g_quark_from_static_string ("lgi-callback-error-quark");
	  g_set_error_literal (err, q, 1, lua_tostring (L, -1));
	  lua_pop (L, 1);
	}

      /* Such function should usually return FALSE, so do it. */
      if (g_type_info_get_tag (callable->retval.ti) == GI_TYPE_TAG_BOOLEAN)
	*(gboolean *) ret = FALSE;
    }

  /* If the closure is marked as autodestroy, destroy it now.  Note that it is
     unfortunately not possible to destroy it directly here, because we would
     delete the code under our feet and crash and burn :-(. Instead, we create
     marshal guard and leave it to GC to destroy the closure later. */
  if (closure->autodestroy)
    *lgi_guard_create (L, lgi_closure_destroy) = block;

  /* This is NOT called by Lua, so we better leave the Lua stack we
     used pretty much tidied. */
  lua_settop (L, stacktop);

  /* Going back to C code, release the state synchronization. */
  lgi_state_leave (block->callback.state_lock);
}

/* Destroys specified closure. */
void
lgi_closure_destroy (gpointer user_data)
{
  FfiClosureBlock* block = user_data;
  lua_State *L = block->callback.L;
  FfiClosure *closure;
  int i;

  for (i = block->closures_count - 1; i >= -1; --i)
    {
      closure = (i < 0) ? &block->ffi_closure : block->ffi_closures[i];
      if (closure->created)
	{
	  luaL_unref (L, LUA_REGISTRYINDEX, closure->callable_ref);
	  luaL_unref (L, LUA_REGISTRYINDEX, closure->target_ref);
	}
      if (i < 0)
	luaL_unref (L, LUA_REGISTRYINDEX, block->callback.thread_ref);
      ffi_closure_free (closure);
    }
}

/* Creates container block for allocated closures.  Returns address of
   the block, suitable as user_data parameter. */
gpointer
lgi_closure_allocate (lua_State *L, int count)
{
  gpointer call_addr;
  int i;

  /* Allocate header block. */
  FfiClosureBlock *block =
    ffi_closure_alloc (offsetof (FfiClosureBlock, ffi_closures)
		       + (--count * sizeof (FfiClosure*)), &call_addr);
  block->ffi_closure.created = 0;
  block->ffi_closure.call_addr = call_addr;
  block->ffi_closure.block = block;
  block->closures_count = count;

  /* Allocate all additional closures. */
  for (i = 0; i < count; ++i)
    {
      block->ffi_closures[i] = ffi_closure_alloc (sizeof (FfiClosure),
						  &call_addr);
      block->ffi_closures[i]->created = 0;
      block->ffi_closures[i]->call_addr = call_addr;
      block->ffi_closures[i]->block = block;
    }

  /* Store reference to target Lua thread. */
  block->callback.L = L;
  lua_pushthread (L);
  block->callback.thread_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Retrieve and remember state lock. */
  block->callback.state_lock = lgi_state_get_lock (L);
  return block;
}

/* Creates closure from Lua function to be passed to C. */
gpointer
lgi_closure_create (lua_State *L, gpointer user_data,
		    int target, gboolean autodestroy)
{
  FfiClosureBlock* block = user_data;
  FfiClosure *closure;
  Callable *callable;
  gpointer call_addr;
  int i;

  /* Find pointer to target FfiClosure. */
  for (closure = &block->ffi_closure, i = 0; closure->created; ++i)
    {
      g_assert (i < block->closures_count);
      closure = block->ffi_closures[i];
    }

  /* Prepare callable and store reference to it. */
  callable = lua_touserdata (L, -1);
  call_addr = closure->call_addr;
  closure->created = 1;
  closure->autodestroy = autodestroy;
  closure->callable_ref = luaL_ref (L, LUA_REGISTRYINDEX);
  if (!lua_isthread (L, target))
    {
      lua_pushvalue (L, target);
      closure->target_ref = luaL_ref (L, LUA_REGISTRYINDEX);
    }
  else
    {
      /* Switch thread_ref to actual target thread. */
      lua_pushvalue (L, target);
      lua_rawseti (L, LUA_REGISTRYINDEX, block->callback.thread_ref);
      closure->target_ref = LUA_NOREF;
    }

  /* Create closure. */
  if (ffi_prep_closure_loc (&closure->ffi_closure, &callable->cif,
			    closure_callback, closure, call_addr) != FFI_OK)
    {
      lua_concat (L, lgi_type_get_name (L, callable->info));
      luaL_error (L, "failed to prepare closure for `%'", lua_tostring (L, -1));
      return NULL;
    }

  return call_addr;
}

/* Creates new Callable instance according to given gi.info. Lua prototype:
   callable = callable.new(callable_info[, addr]) or
   callable = callable.new(description_table[, addr]) */
static int
callable_new (lua_State *L)
{
  gpointer addr = lua_touserdata (L, 2);
  if (lua_istable (L, 1))
    return lgi_callable_parse (L, 1, addr);
  else
    return lgi_callable_create (L,  *(GICallableInfo **)
				  luaL_checkudata (L, 1, LGI_GI_INFO),
				  addr);
}

/* Callable module public API table. */
static const luaL_Reg callable_api_reg[] = {
  { "new", callable_new },
  { NULL, NULL }
};

void
lgi_callable_init (lua_State *L)
{
  /* Register callable metatable. */
  lua_pushlightuserdata (L, &callable_mt);
  lua_newtable (L);
  luaL_register (L, NULL, callable_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Create cache for callables. */
  lgi_cache_create (L, &callable_cache, NULL);

  /* Create public api for callable module. */
  lua_newtable (L);
  luaL_register (L, NULL, callable_api_reg);
  lua_setfield (L, -2, "callable");
}
