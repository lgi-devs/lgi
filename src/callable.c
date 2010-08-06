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

/* See CallableParam::tag description. */
#define LGI_TYPE_TAG_COMPOUND GI_TYPE_TAG_INTERFACE

/* Helper structure representing single parameter (or return value) of callable
   signature. */
struct CallableParam
{
  /* Original typeinfo of this parameter. */
  GITypeInfo ti;

  /* Real typeinfo tag; note that for GI_TYPE_TAG_ENUM types it is already
     resolved to real underlying type. If the type is some kind of compound
     (i.e. object, struct or interface), tag is set to
     LGI_TYPE_TAG_COMPOUND. */
  GITypeTag tag;

  /* GType of object/interface; available only when tag is
     LGI_TYPE_TAG_COMPOUND. */
  GType gtype;

  /* Index of associated Lua input argument or return value, or -1 if there is
     no Lua association for the parameter. Note that input argument 1 is
     always argument of Callable userdata. */
  gint narg : 8;
  gint nret : 8;
};

struct CallableFfi;

/* Structure representing userdata allocated for any callable, i.e. function,
   method, signal, vtable, callback... */
struct Callable
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

  /* Pointer to CallableFfi structure, allocated in the same userdata chunk
     as this instance is. */
  struct CallableFfi* ffi;

  /* '1 (retval) + argc + has_self' slots follow. */
  struct CallableParam params[1 /* nargs + 2 */];
};

/* libffi-related Callable data, appended after Callable (which is also
   variable-length one). */
struct CallableFfi
{
  /* Initialized FFI CIF structure. */
  ffi_cif cif;

  /* '1 (retval) + argc + has_self + throws' slots follow. */
  ffi_type* args[1 /* nargs + 3 */];
};

/* Gets ffi_type for simple types, returns NULL if tag is complex and cannot be
   handled. */
static ffi_type*
get_ffi_type(GITypeTag tag)
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

/* Fills in parameter for specified compound type (object, interface or
   struct). */
static ffi_type*
store_param_compound(GIRegisteredTypeInfo* ci, struct CallableParam* param)
{
  g_assert(GI_IS_OBJECT_INFO(ci) || GI_IS_INTERFACE_INFO(ci) ||
           GI_IS_STRUCT_INFO(ci));

  param->tag = LGI_TYPE_TAG_COMPOUND;
  param->gtype = g_registered_type_info_get_g_type(ci);
  return &ffi_type_pointer;
}

/* Fills in specified parameter and ffitype.  Assumes that param->ti is already
   filled in. */
static ffi_type*
store_param(GITypeInfo* ti, GIDirection direction,
            struct CallableParam* param)
{
  ffi_type* ffi;

  param->tag = g_type_info_get_tag(ti);
  ffi = get_ffi_type(param->tag);
  if (ffi == NULL)
    {
      switch (param->tag)
        {
        case GI_TYPE_TAG_INTERFACE:
          {
            GIBaseInfo* ii = g_type_info_get_interface(ti);
            switch (g_base_info_get_type(ii))
              {
              case GI_INFO_TYPE_ENUM:
              case GI_INFO_TYPE_FLAGS:
                {
                  /* Resolve to real base type. */
                  param->tag = g_enum_info_get_storage_type(ii);
                  ffi = get_ffi_type(param->tag);
                  g_assert(ffi != NULL);
                }
                break;

              case GI_INFO_TYPE_INTERFACE:
              case GI_INFO_TYPE_STRUCT:
              case GI_INFO_TYPE_OBJECT:
                ffi = store_param_compound(ii, param);
                break;

              default:
                /* All other interfaces are passed as pointers. */
                ffi = &ffi_type_pointer;
              }
            g_base_info_unref(ii);
          }
          break;

        case GI_TYPE_TAG_ARRAY:
        case GI_TYPE_TAG_GLIST:
        case GI_TYPE_TAG_GSLIST:
        case GI_TYPE_TAG_GHASH:
          return &ffi_type_pointer;
          break;

        default:
          g_assert_not_reached();
        }
    }

  /* In case that parameter is inout or out, the parameter to ffi is always
     pointer. Also invalidate lua-fields for parameters which are in bad
     direction. */
  switch (direction)
    {
    case GI_DIRECTION_IN:
      param->narg = -1;
      break;

    case GI_DIRECTION_OUT:
      ffi = &ffi_type_pointer;
      param->nret = -1;
      break;

    case GI_DIRECTION_INOUT:
      ffi = &ffi_type_pointer;
      param->narg = -1;
      param->nret = -1;
      break;
    }

  return ffi;
}

int
lgi_callable_store(lua_State* L, GICallableInfo* info)
{
  struct Callable* callable;
  gint nargs, argi, argstart, in_argi = 1, out_argi = 0;
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
  callable =
    lua_newuserdata(L,
                    G_STRUCT_OFFSET(struct Callable, params[nargs + 2]) +
		    G_STRUCT_OFFSET(struct CallableFfi, args[nargs + 3]));
  luaL_getmetatable(L, LGI_CALLABLE);
  lua_setmetatable(L, -2);

  /* Fill in callable with proper contents. */
  callable->ffi = (struct CallableFfi*)&callable->params[nargs + 2];
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
  g_callable_info_load_return_type(callable->info, &callable->params[0].ti);
  store_param(&callable->params[0].ti, GI_DIRECTION_OUT, &callable->params[0]);
  argstart = 1;

  /* Process 'self' argument, if present. */
  if (callable->has_self)
    {
      callable->ffi->args[1] =
        store_param_compound(g_base_info_get_container(callable->info),
                             &callable->params[argi]);
      callable->params[1].narg = -1;
      callable->params[1].nret = 0;
      argstart++;
    }

  /* Process the rest of the arguments. */
  for (argi = 0; argi < nargs; argi++)
    {
      g_callable_info_load_arg(callable->info, argi, &ai);
      g_arg_info_load_type(&ai, &callable->params[argstart + argi].ti);
      callable->ffi->args[argstart + argi] =
        store_param(&callable->params[argstart + argi].ti,
                    g_arg_info_get_direction(&ai),
                    &callable->params[argstart + argi]);
    }

  /* Add ffi info for 'err' argument. */
  if (callable->throws)
    callable->ffi->args[argi++] = &ffi_type_pointer;

  /* Create ffi_cif. */
  if (ffi_prep_cif(&callable->ffi->cif, FFI_DEFAULT_ABI, argi,
                   callable->ffi->args[0], &callable->ffi->args[1]) != FFI_OK)
    {
      lua_concat(L, lgi_type_get_name(L, callable->info));
      return luaL_error(L, "ffi_prep_cif for `%s' failed",
                        lua_tostring(L, -1));
    }

  /* Process callable[args] and reassign narg and nret fields, so that instead
     of 0/-1 they contain real index of lua argument on the stack. */
  for (argi = 0; argi < nargs + 1 + callable->has_self; argi++)
    {
      if (callable->params[argi].narg != 0)
        callable->params[argi].narg = in_argi++;

      if (callable->params[argi].nret != 0)
        callable->params[argi].nret = out_argi++;
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

/* Represents single argument used during function invokation. */
struct Arg
{
  /* Value to which ffi points (real argument obtained by C callee). */
  GArgument val;

  /* Pointer to 'val'; in case of output arguments, ffi actually points to this
     pointer, so that real value is filled into 'val'. */
  GArgument* out_val;
};

int
lgi_callable_call(lua_State* L, gpointer addr, int func_index, int args_index)
{
  struct Callable* callable = luaL_checkudata(L, func_index, LGI_CALLABLE);
  void** args_ffi;
  struct Arg* args;
  gint ffi_args_count, args_start, argi;

  /* Check that we know where to call. */
  if (addr == NULL && callable->address == NULL)
    {
      lua_concat(L, lgi_type_get_name(L, callable->info));
      return luaL_error(L, "`%s': no native addr to call",
                        lua_tostring(L, -1));
    }

  /* Prepare argument arrays. */
  args_start = 1 + callable->has_self;
  ffi_args_count = args_start + callable->nargs + callable->throws;
  args = g_newa(struct Arg, ffi_args_count);
  args_ffi = g_newa(void*, ffi_args_count);
  for (argi = 0; argi < ffi_args_count; argi++)
    args_ffi[argi] = &args[argi];

  return 0;
}

static int
callable_gc(lua_State* L)
{
  /* Just unref embedded 'info' field. */
  struct Callable* callable = luaL_checkudata(L, 1, LGI_CALLABLE);
  g_base_info_unref(callable->info);
  return 0;
}

static int
callable_tostring(lua_State* L)
{
  struct Callable* callable = luaL_checkudata(L, 1, LGI_CALLABLE);
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
