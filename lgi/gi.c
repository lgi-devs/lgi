/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Native Lua wrappers around GIRepository.
 */

#include <string.h>
#include "lgi.h"

typedef GIBaseInfo *(* InfosItemGet)(GIBaseInfo* info, gint item);

/* Creates new instance of info from given GIBaseInfo pointer. */
int
lgi_gi_info_new (lua_State *L, GIBaseInfo *info)
{
  if (info)
    {
      GIBaseInfo **ud_info;

      if (g_base_info_get_type (info) == GI_INFO_TYPE_INVALID)
	{
	  g_base_info_unref (info);
	  lua_pushnil (L);
	}
      else
	{
	  ud_info = lua_newuserdata (L, sizeof (info));
	  *ud_info = info;
	  luaL_getmetatable (L, LGI_GI_INFO);
	  lua_setmetatable (L, -2);
	}
    }
  else
    lua_pushnil (L);

  return 1;
}

gpointer
lgi_gi_load_function (lua_State *L, int typetable, const char *name)
{
  GIBaseInfo **info;
  gpointer symbol = NULL;

  luaL_checkstack (L, 3, "");
  lua_getfield (L, typetable, name);
  info = lgi_udata_test (L, -1, LGI_GI_INFO);
  if (info && GI_IS_FUNCTION_INFO (*info))
      g_typelib_symbol (g_base_info_get_typelib (*info),
                        g_function_info_get_symbol (*info), &symbol);
  else if (lua_islightuserdata (L, -1))
    symbol = lua_touserdata (L, -1);
  lua_pop (L, 1);
  return symbol;
}

/* Userdata representing single group of infos (e.g. methods on
   object, fields of struct etc.).  Emulates Lua table for access. */
typedef struct _Infos
{
  GIBaseInfo *info;
  gint count;
  InfosItemGet item_get;
} Infos;
#define LGI_GI_INFOS "lgi.gi.infos"

static int
infos_len (lua_State *L)
{
  Infos* infos = luaL_checkudata (L, 1, LGI_GI_INFOS);
  lua_pushnumber (L, infos->count);
  return 1;
}

static int
infos_index (lua_State *L)
{
  Infos* infos = luaL_checkudata (L, 1, LGI_GI_INFOS);
  gint n;
  if (lua_type (L, 2) == LUA_TNUMBER)
    {
      n = lua_tonumber (L, 2) - 1;
      luaL_argcheck (L, n >= 0 && n < infos->count, 2, "out of bounds");
      return lgi_gi_info_new (L, infos->item_get (infos->info, n));
    }
  else
    {
      const gchar *name = luaL_checkstring (L, 2);
      for (n = 0; n < infos->count; n++)
	{
	  GIBaseInfo *info = infos->item_get (infos->info, n);
	  if (strcmp (g_base_info_get_name (info), name) == 0)
	    return lgi_gi_info_new (L, info);

	  g_base_info_unref (info);
	}

      lua_pushnil (L);
      return 1;
    }
}

static int
infos_gc (lua_State *L)
{
  Infos *infos = luaL_checkudata (L, 1, LGI_GI_INFOS);
  g_base_info_unref (infos->info);
  return 0;
}

/* Creates new userdata object representing given category of infos. */
static int
infos_new (lua_State *L, GIBaseInfo *info, gint count, InfosItemGet item_get)
{
  Infos *infos = lua_newuserdata (L, sizeof (Infos));
  luaL_getmetatable (L, LGI_GI_INFOS);
  lua_setmetatable (L, -2);
  infos->info = g_base_info_ref (info);
  infos->count = count;
  infos->item_get = item_get;
  return 1;
}

static const luaL_Reg gi_infos_reg[] = {
  { "__gc", infos_gc },
  { "__len", infos_len },
  { "__index", infos_index },
  { NULL, NULL }
};

static int
info_push_transfer (lua_State *L, GITransfer transfer)
{
  if (0);
#define H(n1, n2)				\
  else if (transfer == GI_TRANSFER_ ## n1)	\
    {						\
      lua_pushstring (L, #n2);			\
      return 1;					\
    }
  H(NOTHING, none)
    H(CONTAINER, container)
    H(EVERYTHING, full)
#undef H
    return 0;
}

static int
info_index (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, LGI_GI_INFO);
  const gchar *prop = luaL_checkstring (L, 2);

#define INFOS(n1, n2)							\
  else if (strcmp (prop, #n2 "s") == 0)					\
    return infos_new (L, *info,						\
		      g_ ## n1 ## _info_get_n_ ## n2 ## s (*info),	\
		      g_ ## n1 ## _info_get_ ## n2);

#define INFOS2(n1, n2, n3)					\
  else if (strcmp (prop, #n3) == 0)				\
    return infos_new (L, *info,					\
		      g_ ## n1 ## _info_get_n_ ## n3 (*info),	\
		      g_ ## n1 ## _info_get_ ## n2);

  if (strcmp (prop, "type") == 0)
    {
      switch (g_base_info_get_type (*info))
	{
#define H(n1, n2)				\
	  case GI_INFO_TYPE_ ## n1:		\
	    lua_pushstring (L, #n2);		\
	    return 1;

	  H(FUNCTION, function)
	    H(CALLBACK, callback)
	    H(STRUCT, struct)
	    H(BOXED, boxed)
	    H(ENUM, enum)
	    H(FLAGS, flags)
	    H(OBJECT, object)
	    H(INTERFACE, interface)
	    H(CONSTANT, constant)
	    H(UNION, union)
	    H(VALUE, value)
	    H(SIGNAL, signal)
	    H(VFUNC, vfunc)
	    H(PROPERTY, property)
	    H(FIELD, field)
	    H(ARG, arg)
	    H(TYPE, type)
	    H(UNRESOLVED, unresolved)
#undef H
	default:
	  g_assert_not_reached ();
	}
    }

#define H(n1, n2)						\
  if (strcmp (prop, "is_" #n2) == 0)				\
    {								\
      lua_pushboolean (L, GI_IS_ ## n1 ## _INFO (*info));	\
      return 1;							\
    }
  H(ARG, arg)
    H(CALLABLE, callable)
    H(FUNCTION, function)
    H(SIGNAL, signal)
    H(VFUNC, vfunc)
    H(CONSTANT, constant)
    H(FIELD, field)
    H(PROPERTY, property)
    H(REGISTERED_TYPE, registered_type)
    H(ENUM, enum)
    H(INTERFACE, interface)
    H(OBJECT, object)
    H(STRUCT, struct)
    H(UNION, union)
    H(TYPE, type)
    H(VALUE, value);
#undef H

  if  (!GI_IS_TYPE_INFO (*info))
    {
      if (strcmp (prop, "name") == 0)
	{
	  lua_pushstring (L, g_base_info_get_name (*info));
	  return 1;
	}
      else if (strcmp (prop, "namespace") == 0)
	{
	  lua_pushstring (L, g_base_info_get_namespace (*info));
	  return 1;
	}
    }

  if (strcmp (prop, "fullname") == 0)
    {
      lua_concat (L, lgi_type_get_name (L, *info));
      return 1;
    }

  if (strcmp (prop, "deprecated") == 0)
    {
      lua_pushboolean (L, g_base_info_is_deprecated (*info));
      return 1;
    }
  else if (strcmp (prop, "container") == 0)
    {
      GIBaseInfo *container = g_base_info_get_container (*info);
      if (container)
	g_base_info_ref (container);
      return lgi_gi_info_new (L, container);
    }
  else if (strcmp (prop, "typeinfo") == 0)
    {
      GITypeInfo *ti = NULL;
      if (GI_IS_ARG_INFO (*info))
	ti = g_arg_info_get_type (*info);
      else if (GI_IS_CONSTANT_INFO (*info))
	ti = g_constant_info_get_type (*info);
      else if (GI_IS_PROPERTY_INFO (*info))
	ti = g_property_info_get_type (*info);
      else if (GI_IS_FIELD_INFO (*info))
	ti = g_field_info_get_type (*info);

      if (ti)
	return lgi_gi_info_new (L, ti);
    }

  if (GI_IS_REGISTERED_TYPE_INFO (*info))
    {
      if (strcmp (prop, "gtype") == 0)
	{
	  GType gtype = g_registered_type_info_get_g_type (*info);
	  if (gtype != G_TYPE_NONE)
	    lua_pushlightuserdata (L, (void *) gtype);
	  else
	    lua_pushnil (L);
	  return 1;
	}
      else if (GI_IS_STRUCT_INFO (*info))
	{
	  if (strcmp (prop, "is_gtype_struct") == 0)
	    {
	      lua_pushboolean (L, g_struct_info_is_gtype_struct (*info));
	      return 1;
	    }
	  else if (strcmp (prop, "size") == 0)
	    {
	      lua_pushnumber (L, g_struct_info_get_size (*info));
	      return 1;
	    }
	  INFOS (struct, field)
	    INFOS (struct, method);
	}
      else if (GI_IS_UNION_INFO (*info))
	{
	  if (strcmp (prop, "size") == 0)
	    {
	      lua_pushnumber (L, g_struct_info_get_size (*info));
	      return 1;
	    }
	  INFOS (union, field)
	    INFOS (union, method);
	}
      else if (GI_IS_INTERFACE_INFO (*info))
	{
	  if (strcmp (prop, "type_struct") == 0)
	    return
	      lgi_gi_info_new (L, g_interface_info_get_iface_struct (*info));
	  INFOS (interface, prerequisite)
	    INFOS (interface, vfunc)
	    INFOS (interface, method)
	    INFOS (interface, constant)
	    INFOS2 (interface, property, properties)
	    INFOS (interface, signal);
	}
      else if (GI_IS_OBJECT_INFO (*info))
	{
	  if (strcmp (prop, "parent") == 0)
	    return lgi_gi_info_new (L, g_object_info_get_parent (*info));
	  else if (strcmp (prop, "type_struct") == 0)
	    return lgi_gi_info_new (L, g_object_info_get_class_struct (*info));
	  INFOS (object, interface)
	    INFOS (object, field)
	    INFOS (object, vfunc)
	    INFOS (object, method)
	    INFOS (object, constant)
	    INFOS2 (object, property, properties)
	    INFOS (object, signal);
	}
    }

  if (GI_IS_CALLABLE_INFO (*info))
    {
      if (strcmp (prop, "return_type") == 0)
	return lgi_gi_info_new (L, g_callable_info_get_return_type (*info));
      else if (strcmp (prop, "return_transfer") == 0)
	return info_push_transfer (L, g_callable_info_get_caller_owns (*info));
      INFOS (callable, arg);

      if (GI_IS_SIGNAL_INFO (*info))
	{
	  if (strcmp (prop, "flags") == 0)
	    {
	      GSignalFlags flags = g_signal_info_get_flags (*info);
	      lua_newtable (L);
#define H(n1, n2)					\
	      if ((flags & G_SIGNAL_ ## n1) != 0)	\
		{					\
		  lua_pushboolean (L, 1);		\
		  lua_setfield (L, -2, #n2);		\
		}
	      H(RUN_FIRST, run_first)
		H(RUN_LAST, run_last)
		H(RUN_CLEANUP, run_cleanup)
		H(NO_RECURSE, no_recurse)
		H(DETAILED, detailed)
		H(ACTION, action)
		H(NO_HOOKS, no_hooks);
#undef H
	      return 1;
	    }
	}

      if (GI_IS_FUNCTION_INFO (*info))
	{
	  if (strcmp (prop, "flags") == 0)
	    {
	      GIFunctionInfoFlags flags = g_function_info_get_flags (*info);
	      lua_newtable (L);
	      if (0);
#define H(n1, n2)					\
	      else if ((flags & GI_FUNCTION_ ## n1) != 0)	\
		{					\
		  lua_pushboolean (L, 1);		\
		  lua_setfield (L, -2, #n2);		\
		}
	      H(IS_METHOD, is_method)
		H(IS_CONSTRUCTOR, is_constructor)
		H(IS_GETTER, is_getter)
		H(IS_SETTER, is_setter)
		H(WRAPS_VFUNC, wraps_vfunc)
		H(THROWS, throws);
#undef H
	      return 1;
	    }
	}
    }

  if (GI_IS_ENUM_INFO (*info))
    {
      if (strcmp (prop, "storage") == 0)
	{
	  GITypeTag tag = g_enum_info_get_storage_type (*info);
	  lua_pushstring (L, g_type_tag_to_string (tag));
	  return 1;
	}
#if GLIB_CHECK_VERSION (2, 30, 0)
      INFOS (enum, method)
#endif
	INFOS (enum, value)
      else if (strcmp (prop, "error_domain") == 0)
	{
	  const gchar *domain = g_enum_info_get_error_domain (*info);
	  if (domain != NULL)
	    lua_pushnumber (L, g_quark_from_string (domain));
	  else
	    lua_pushnil (L);

	  return 1;
	}
    }

  if (GI_IS_VALUE_INFO (*info))
    {
      if (strcmp (prop, "value") == 0)
	{
	  lua_pushnumber (L, g_value_info_get_value (*info));
	  return 1;
	}
    }

  if (GI_IS_ARG_INFO (*info))
    {
      if (strcmp (prop, "direction") == 0)
	{
	  GIDirection dir = g_arg_info_get_direction (*info);
	  if (dir == GI_DIRECTION_OUT)
	    lua_pushstring (L, g_arg_info_is_caller_allocates (*info)
			    ? "out-caller-alloc" : "out");
	  else
	    lua_pushstring (L, dir == GI_DIRECTION_IN ? "in" : "inout");
	  return 1;
	}
      if (strcmp (prop, "transfer") == 0)
	return info_push_transfer (L,
				   g_arg_info_get_ownership_transfer (*info));
      if (strcmp (prop, "optional") == 0)
	{
	  lua_pushboolean (L, g_arg_info_is_optional (*info)
			   || g_arg_info_may_be_null (*info));
	  return 1;
	}
    }

  if (GI_IS_PROPERTY_INFO (*info))
    {
      if (strcmp (prop, "flags") == 0)
	{
	  lua_pushnumber (L, g_property_info_get_flags (*info));
	  return 1;
	}
      else if (strcmp (prop, "transfer") == 0)
	return
	  info_push_transfer (L,
			      g_property_info_get_ownership_transfer (*info));
    }

  if (GI_IS_FIELD_INFO (*info))
    {
      if (strcmp (prop, "flags") == 0)
	{
	  GIFieldInfoFlags flags = g_field_info_get_flags (*info);
	  lua_newtable (L);
	  if (0);
#define H(n1, n2)					\
	      else if ((flags & GI_FIELD_ ## n1) != 0)	\
		{					\
		  lua_pushboolean (L, 1);		\
		  lua_setfield (L, -2, #n2);		\
		}
	      H(IS_READABLE, is_readable)
		H(IS_WRITABLE, is_writable)
#undef H
	      return 1;
	}
      else if (strcmp (prop, "size") == 0)
	{
	  lua_pushnumber (L, g_field_info_get_size (*info));
	  return 1;
	}
      else if (strcmp (prop, "offset") == 0)
	{
	  lua_pushnumber (L, g_field_info_get_offset (*info));
	  return 1;
	}
    }

  if (GI_IS_TYPE_INFO (*info))
    {
      GITypeTag tag = g_type_info_get_tag (*info);
      if (strcmp (prop, "tag") == 0)
	{
	  lua_pushstring (L, g_type_tag_to_string (tag));
	  return 1;
	}
      else if (strcmp (prop, "is_basic") == 0)
	{
	  lua_pushboolean (L, G_TYPE_TAG_IS_BASIC (tag));
	  return 1;
	}
      else if (strcmp (prop, "params") == 0)
	{
	  if (tag == GI_TYPE_TAG_ARRAY || tag == GI_TYPE_TAG_GLIST ||
	      tag == GI_TYPE_TAG_GSLIST || tag == GI_TYPE_TAG_GHASH)
	    {
	      lua_newtable (L);
	      lgi_gi_info_new (L, g_type_info_get_param_type (*info, 0));
	      lua_rawseti (L, -2, 1);
	      if (tag == GI_TYPE_TAG_GHASH)
		{
		  lgi_gi_info_new (L, g_type_info_get_param_type (*info, 1));
		  lua_rawseti (L, -2, 2);
		}
	      return 1;
	    }
	}
      else if (strcmp (prop, "interface") == 0 && tag == GI_TYPE_TAG_INTERFACE)
	{
	  lgi_gi_info_new (L, g_type_info_get_interface (*info));
	  return 1;
	}
      else if (strcmp (prop, "array_type") == 0 && tag == GI_TYPE_TAG_ARRAY)
	{
	  switch (g_type_info_get_array_type (*info))
	    {
#define H(n1, n2)				\
	      case GI_ARRAY_TYPE_ ## n1:	\
		lua_pushstring (L, #n2);	\
		return 1;

	      H(C, c)
		H(ARRAY, array)
		H(PTR_ARRAY, ptr_array)
		H(BYTE_ARRAY, byte_array)
#undef H
	    default:
	      g_assert_not_reached ();
	    }
	}
      else if (strcmp (prop, "is_zero_terminated") == 0
	       && tag == GI_TYPE_TAG_ARRAY)
	{
	  lua_pushboolean (L, g_type_info_is_zero_terminated (*info));
	  return 1;
	}
      else if (strcmp (prop, "array_length") == 0)
	{
	  int len = g_type_info_get_array_length (*info);
	  if (len >= 0)
	    {
	      lua_pushnumber (L, len);
	      return 1;
	    }
	}
      else if (strcmp (prop, "fixed_size") == 0)
	{
	  int size = g_type_info_get_array_fixed_size (*info);
	  if (size >= 0)
	    {
	      lua_pushnumber (L, size);
	      return 1;
	    }
	}
      else if (strcmp (prop, "is_pointer") == 0)
	{
	  lua_pushboolean (L, g_type_info_is_pointer (*info));
	  return 1;
	}
    }

  lua_pushnil (L);
  return 1;

#undef INFOS
#undef INFOS2
}

static int
info_eq (lua_State *L)
{
  GIBaseInfo **i1 = luaL_checkudata (L, 1, LGI_GI_INFO);
  GIBaseInfo **i2 = luaL_checkudata (L, 2, LGI_GI_INFO);
  lua_pushboolean (L, g_base_info_equal (*i1, *i2));
  return 1;
}

static int
info_gc (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, LGI_GI_INFO);
  g_base_info_unref (*info);
  return 0;
}

static const luaL_Reg gi_info_reg[] = {
  { "__gc", info_gc },
  { "__index", info_index },
  { "__eq", info_eq },
  { NULL, NULL }
};

/* Userdata representing symbol resolver of the namespace. */
#define LGI_GI_RESOLVER "lgi.gi.resolver"

static int
resolver_index (lua_State *L)
{
  gpointer address;
  GITypelib **typelib = luaL_checkudata (L, 1, LGI_GI_RESOLVER);
  if (g_typelib_symbol (*typelib, luaL_checkstring (L, 2), &address))
    {
      lua_pushlightuserdata (L, address);
      return 1;
    }

  return 0;
}

static const luaL_Reg gi_resolver_reg[] = {
  { "__index", resolver_index },
  { NULL, NULL }
};

/* Userdata representing namespace in girepository. */
#define LGI_GI_NAMESPACE "lgi.gi.namespace"

static int
namespace_len (lua_State *L)
{
  const gchar *ns = luaL_checkudata (L, 1, LGI_GI_NAMESPACE);
  lua_pushnumber (L, g_irepository_get_n_infos (NULL, ns));
  return 1;
}

static int
namespace_index (lua_State *L)
{
  const gchar *ns = luaL_checkudata (L, 1, LGI_GI_NAMESPACE);
  const gchar *prop;
  if (lua_type (L, 2) == LUA_TNUMBER)
    {
      GIBaseInfo *info = g_irepository_get_info (NULL, ns,
						 lua_tointeger (L, 2) - 1);
      return lgi_gi_info_new (L, info);
    }
  prop = luaL_checkstring (L, 2);
  if (strcmp (prop, "dependencies") == 0)
    {
      gchar **deps = g_irepository_get_dependencies (NULL, ns);
      if (deps == NULL)
	lua_pushnil (L);
      else
	{
	  int index;
	  gchar **dep;
	  lua_newtable (L);
	  for (index = 1, dep = deps; *dep; dep++, index++)
	    {
	      const gchar *sep = strchr (*dep, '-');
	      lua_pushlstring (L, *dep, sep - *dep);
	      lua_pushstring (L, sep + 1);
	      lua_settable (L, -3);
	    }
	  g_strfreev (deps);
	}

      return 1;
    }
  else if (strcmp (prop, "version") == 0)
    {
      lua_pushstring (L, g_irepository_get_version (NULL, ns));
      return 1;
    }
  else if (strcmp (prop, "name") == 0)
    {
      lua_pushstring (L, ns);
      return 1;
    }
  else if (strcmp (prop, "resolve") == 0)
    {
      GITypelib **udata = lua_newuserdata (L, sizeof (GITypelib *));
      luaL_getmetatable (L, LGI_GI_RESOLVER);
      lua_setmetatable (L, -2);
      *udata = g_irepository_require (NULL, ns, NULL, 0, NULL);
      return 1;
    }
  else
    /* Try to lookup the symbol. */
    return lgi_gi_info_new (L, g_irepository_find_by_name (NULL, ns, prop));
}

static int
namespace_new (lua_State *L, const gchar *namespace)
{
  gchar *ns = lua_newuserdata (L, strlen (namespace) + 1);
  luaL_getmetatable (L, LGI_GI_NAMESPACE);
  lua_setmetatable (L, -2);
  strcpy (ns, namespace);
  return 1;
}

static const luaL_Reg gi_namespace_reg[] = {
  { "__index", namespace_index },
  { "__len", namespace_len },
  { NULL, NULL }
};

/* Lua API: core.gi.require(namespace[, version[, typelib_dir]]) */
static int
gi_require (lua_State *L)
{
  GError *err = NULL;
  const gchar *namespace = luaL_checkstring (L, 1);
  const gchar *version = luaL_optstring (L, 2, NULL);
  const gchar *typelib_dir = luaL_optstring (L, 3, NULL);
  GITypelib *typelib;

  if (typelib_dir == NULL)
    typelib = g_irepository_require (NULL, namespace, version, 0, &err);
  else
    typelib = g_irepository_require_private (NULL, typelib_dir, namespace,
					     version, 0, &err);
  if (!typelib)
    {
      lua_pushboolean (L, 0);
      lua_pushstring (L, err->message);
      lua_pushnumber (L, err->code);
      g_error_free (err);
      return 3;
    }

  return namespace_new (L, namespace);
}

/* Lua API: boolean = core.gi.isinfo(info) */
static int
gi_isinfo (lua_State *L)
{
  if (lua_getmetatable (L, 1))
    {
      luaL_getmetatable (L, LGI_GI_INFO);
      lua_pushboolean (L, lua_rawequal (L, -1, -2));
    }
  else
    lua_pushboolean (L, 0);
  return 1;
}

static int
gi_index (lua_State *L)
{
  if (lua_type (L, 2) == LUA_TLIGHTUSERDATA)
    {
      GType gtype = (GType) lua_touserdata (L, 2);
      GIBaseInfo *info = (gtype != G_TYPE_INVALID)
	? g_irepository_find_by_gtype (NULL, gtype) : NULL;
      return lgi_gi_info_new (L, info);
    }
  else if (lua_type (L, 2) == LUA_TNUMBER)
    {
      GQuark domain = (GQuark) lua_tonumber (L, 2);
      GIBaseInfo *info = g_irepository_find_by_error_domain (NULL, domain);
      return lgi_gi_info_new (L, info);
    }
  else
    {
      const gchar *ns = luaL_checkstring (L, 2);
      if (g_irepository_is_registered (NULL, ns, NULL))
	return namespace_new (L, ns);
    }

  return 0;
}

typedef struct _Reg
{
  const gchar *name;
  const luaL_Reg* reg;
} Reg;

static const Reg gi_reg[] = {
  { LGI_GI_INFOS, gi_infos_reg },
  { LGI_GI_INFO, gi_info_reg },
  { LGI_GI_NAMESPACE, gi_namespace_reg },
  { LGI_GI_RESOLVER, gi_resolver_reg },
  { NULL, NULL }
};

static const luaL_Reg gi_api_reg[] = {
  { "require", gi_require },
  { "isinfo", gi_isinfo },
  { NULL, NULL }
};

void
lgi_gi_init (lua_State *L)
{
  const Reg *reg;

  /* Register metatables for userdata objects. */
  for (reg = gi_reg; reg->name; reg++)
    {
      luaL_newmetatable (L, reg->name);
      luaL_register (L, NULL, reg->reg);
      lua_pop (L, 1);
    }

  /* Register global API. */
  lua_newtable (L);
  luaL_register (L, NULL, gi_api_reg);
  lua_newtable (L);
  lua_pushcfunction (L, gi_index);
  lua_setfield (L, -2, "__index");
  lua_setmetatable (L, -2);
  lua_setfield (L, -2, "gi");
}

#if !GLIB_CHECK_VERSION(2, 30, 0)
/* Workaround for broken g_struct_info_get_size() for GValue, see
   https://bugzilla.gnome.org/show_bug.cgi?id=657040 */
static GIStructInfo *parameter_info = NULL;
static GIFieldInfo *parameter_value_info = NULL;

#undef g_struct_info_get_size
gsize
lgi_struct_info_get_size (GIStructInfo *info)
{
  if (parameter_info == NULL)
    parameter_info = g_irepository_find_by_name (NULL, "GObject", "Parameter");
  if (g_registered_type_info_get_g_type (info) == G_TYPE_VALUE)
    return sizeof (GValue);
  else if (parameter_info && g_base_info_equal (info, parameter_info))
    return sizeof (GParameter);
  return g_struct_info_get_size (info);
}

#undef g_field_info_get_offset
gint
lgi_field_info_get_offset (GIFieldInfo *info)
{
  if (parameter_value_info == NULL)
    {
      if (parameter_info == NULL)
	parameter_info = g_irepository_find_by_name (NULL,
						     "GObject", "Parameter");
      parameter_value_info = g_struct_info_get_field (parameter_info, 1);
    }
  if (parameter_value_info && g_base_info_equal (info, parameter_value_info))
    return G_STRUCT_OFFSET (GParameter, value);
  return g_field_info_get_offset (info);
}
#endif
