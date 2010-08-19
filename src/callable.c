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

/* Context of single Lua->gobject call. */
typedef struct _Call
{
  /* Callable instance. */
  Callable* callable;

  /* Index of Lua stack where Lua arguments for the method begin. */
  int narg;

  /* Call arguments. */
  GArgument* args;

  /* Argument indirection for OUT and INOUT arguments. */
  GArgument** redirect_out;

  /* libffi argument array. */
  void** ffi_args;

  /* Followed by:
     args -> GArgument[callable->nargs + 2];
     redirect_out -> GArgument*[callable->nargs + 2];
     ffi_args -> void*[callable->nargs + 3]; */
} Call;

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
  GIArgInfo ai;

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
  for (argi = 0; argi < 1 + callable->has_self + nargs; argi++)
    callable->params[argi].internal = FALSE;

  /* Process return value. */
  param = &callable->params[0];
  ffi_arg = &callable->ffi_args[0];
  g_callable_info_load_return_type(callable->info, &param->ti);
  param->transfer = g_callable_info_get_caller_owns(callable->info);
  param->caller_alloc = FALSE;
  param->optional = FALSE;
  *ffi_arg = get_ffi_type(param);
  param->dir = GI_DIRECTION_OUT;
  param++;
  ffi_arg++;

  /* Process 'self' argument, if present. */
  if (callable->has_self)
    {
      callable->ffi_args[1] = &ffi_type_pointer;
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
      *ffi_arg = (param->dir == GI_DIRECTION_IN) ?
	get_ffi_type(param) : &ffi_type_pointer;
    }

  /* Add ffi info for 'err' argument. */
  if (callable->throws)
    *ffi_arg++ = &ffi_type_pointer;

  /* Create ffi_cif. */
  if (ffi_prep_cif(&callable->cif, FFI_DEFAULT_ABI,
		   callable->has_self + nargs + callable->throws,
		   callable->ffi_args[0], &callable->ffi_args[1]) != FFI_OK)
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

/* Marshals simple types to/from lua.  Simple are number and strings. Returns 1
   if value was handled, 0 otherwise. */
static int
marshal_simple(lua_State* L, gboolean to_c, int arg, gboolean optional,
	       GITypeTag tag, GArgument* val)
{
  int nret = 1;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)		\
      case tag:							\
	if (to_c)						\
	  val->argf = (ctype)(optional ?			\
			      opt(L, arg, 0) : check(L, arg));	\
	else							\
	  push(L, val->argf);					\
	break;
#include "decltype.h"

    default:
      nret = 0;
    }

  return nret;
}

/* Returns int value of specified parameter.  If specified parameter does not
   exist or its value cannot be converted to int, FALSE is returned. */
static gboolean
get_int_param(Call* call, int param, int *val)
{
  GArgument* arg;
  int argi;

  if (param > 0 && param <= call->callable->nargs)
    {
      argi = 1 + call->callable->has_self + (param - 1);
      arg = &call->args[argi];
      switch (g_type_info_get_tag(&call->callable->params[argi].ti))
        {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)		\
          case tag:                                             \
            *val = (int) arg->argf;                             \
            return TRUE;
#define DECLTYPE_NUMERIC_ONLY
#include "decltype.h"

        default:
          break;
        }
    }

  return FALSE;
}

static gsize
get_type_size(GITypeTag tag)
{
  gsize size;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)              \
      case tag:							\
	size = sizeof(ctype);					\
	break;
#include "decltype.h"

    default:
      size = sizeof(gpointer);
    }

  return size;
}

static int marshal(lua_State* L, gboolean to_c, Call* call, Param* param,
                   GArgument* val, int lua_arg);

static int
marshal_to_carray(lua_State* L, Call* call, Param* param, GArgument* val,
                  int lua_arg)
{
  return 0;
}

static int
marshal_from_carray(lua_State* L, Call* call, Param* param, GArgument* val,
                      int lua_arg)
{
  gint len, index;

  /* First of all, find out the length of the array. */
  if (g_type_info_is_zero_terminated(&param->ti))
    len = -1;
  else
    {
      len = g_type_info_get_array_fixed_size(&param->ti);
      if (len == -1)
        {
          /* Length of the array is dynamic. get it from other argument. */
          if (call == NULL)
            return 0;

          len = g_type_info_get_array_length(&param->ti);
          if (!get_int_param(call, len, &len))
            return 0;
        }
    }

  /* Get pointer to array data. */
  if (val->v_pointer == NULL)
    /* NULL array is represented by nil. */
    lua_pushnil(L);
  else
    {
      GITypeInfo* eti = g_type_info_get_param_type(&param->ti, 0);
      GITypeTag etag = g_type_info_get_tag(eti);
      gsize size = get_type_size(etag);

      /* Create Lua table which will hold the array. */
      lua_createtable(L, len > 0 ? len : 0, 0);

      /* Iterate through array elements. */
      for (index = 0; len < 0 || index < len; index++)
	{
	  /* Get value from specified index. */
          Param eparam;
	  gint offset = index * size;
	  GArgument* eval = (GArgument*)((char*)val->v_pointer + offset);

	  /* If the array is zero-terminated, terminate now and don't
	     include NULL entry. */
	  if (len < 0 && eval->v_pointer == NULL)
	    break;

	  /* Store value into the table. */
          eparam.ti = param->ti;
          eparam.dir = GI_DIRECTION_OUT;
          eparam.transfer = (param->transfer == GI_TRANSFER_EVERYTHING) ?
            GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING;
          eparam.caller_alloc = FALSE;
          eparam.optional = TRUE;
          if (marshal(L, FALSE, NULL, &eparam, eval, 0) == 1)
	    lua_rawseti(L, -2, index + 1);
	}

      /* If needed, free the array itself. */
      if (param->transfer != GI_TRANSFER_NOTHING)
        g_free(val->v_pointer);

      /* Free element's typeinfo. */
      g_base_info_unref(eti);
    }

  return 1;
}

/* Converts given argument to/from Lua.	 Returns 1 value handled, 0
   otherwise. */
static int
marshal(lua_State* L, gboolean to_c, Call* call, Param* param, GArgument* val,
        int lua_arg)
{
  GITypeTag tag = g_type_info_get_tag(&param->ti);
  int nret = marshal_simple(L, to_c, lua_arg, param->optional, tag, val);
  if (nret == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_INTERFACE:
	  {
	    GIBaseInfo* ii = g_type_info_get_interface(&param->ti);
	    GIInfoType type = g_base_info_get_type(ii);
	    switch (type)
	      {
	      case GI_INFO_TYPE_ENUM:
	      case GI_INFO_TYPE_FLAGS:
		/* Store underlying value directly. */
		nret = marshal_simple(L, to_c, lua_arg, param->optional,
				      g_enum_info_get_storage_type(ii), val);
		break;

              case GI_TYPE_TAG_ARRAY:
                {
                  GIArrayType atype = g_type_info_get_array_type(&param->ti);
                  switch (atype)
                    {
                    case GI_ARRAY_TYPE_C:
                      nret = (to_c ?
                              marshal_to_carray(L, call, param,
                                                val, lua_arg) :
                              marshal_from_carray(L, call, param,
                                                  val, lua_arg));
                      break;

                    default:
                      g_warning("bad array type %d", atype);
                    }
                }
                break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_OBJECT:
		if (to_c)
		  {
		    val->v_pointer = lgi_compound_get(L, lua_arg, ii,
						      param->optional);
		    nret = 1;
		  }
		else
		  nret = lgi_compound_create(L, ii, val->v_pointer,
					     param->transfer);
		break;

	      default:
		g_warning("bad typeinfo interface type %d", type);
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	default:
	  g_warning("bad typeinfo tag %d", tag);
	}
    }

  return nret;
}

int
lgi_callable_call(lua_State* L, gpointer addr, int func_index, int args_index)
{
  Call* call;
  Param* param;
  int argi, i, lua_argi, nret;
  GError* err = NULL;
  Callable* callable = luaL_checkudata(L, func_index, LGI_CALLABLE);

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

#ifndef NDEBUG
  lua_concat(L, lgi_type_get_name(L, callable->info));
  g_debug("calling `%s'", lua_tostring(L, -1));
  lua_pop(L, 1);
#endif

  /* Allocate (on the stack) and fill in Call instance. */
  call = g_alloca(sizeof(Call) +
		  sizeof(GArgument) * (callable->nargs + 2) * 2 +
		  sizeof(void*) * (callable->nargs + 3));
  call->callable = callable;
  call->narg = args_index;
  call->args = (GArgument*)&call[1];
  call->redirect_out = (GArgument**)&call->args[callable->nargs + 2];
  call->ffi_args = (void**)&call->redirect_out[callable->nargs + 2];

  /* Prepare return value. */
  call->ffi_args[0] = &call->args[0];

  /* Prepare 'self', if present. */
  lua_argi = args_index;
  argi = 1;
  param = &callable->params[1];
  if (callable->has_self)
    {
      call->args[1].v_pointer =
	lgi_compound_get(L, args_index,
			 g_base_info_get_container(callable->info), TRUE);
      call->ffi_args[1] = &call->args[1];
      argi++;
      lua_argi++;
    }

  /* Process input parameters. */
  for (i = 0; i < callable->nargs; i++, param++, argi++)
    {
      /* Prepare ffi_args and redirection for out/inout parameters. */
      if (param->dir == GI_DIRECTION_IN)
	call->ffi_args[argi] = &call->args[argi];
      else
	{
	  call->ffi_args[argi] = &call->redirect_out[argi];
	  call->redirect_out[argi] = &call->args[argi];
	}

      if (!param->internal && param->dir != GI_DIRECTION_OUT)
	/* Convert parameter from Lua stack to C. */
	marshal(L, TRUE, call, &callable->params[argi],
                &call->args[argi], lua_argi++);
    }

  /* Add error for 'throws' type function. */
  if (callable->throws)
      call->ffi_args[argi++] = &err;

  /* Call the function. */
  ffi_call(&callable->cif, addr, call->ffi_args[0], &call->ffi_args[1]);

  /* Check, whether function threw. */
  if (err != NULL)
    return lgi_error(L, err);

  /* Handle return value. */
  nret = 0;
  if (g_type_info_get_tag(&callable->params[0].ti) != GI_TYPE_TAG_VOID)
    nret = marshal(L, FALSE, call, &callable->params[0], &call->args[0], 0);

  /* Skip 'self' parameter. */

  /* Process output parameters. */

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
