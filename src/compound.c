/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Management of compounds, i.e. structs, unions, objects interfaces, wrapped
 * into single Lua userdata block called 'compound'.
 */

#include <string.h>
#include "lgi.h"

/* 'compound' userdata: wraps compound with reference to its repo table. */
typedef struct _Compound
{
  /* Address of the structure data. */
  gpointer addr;

  /* Lua reference to repo table representing this compound. */
  int ref_repo : 30;

  /* Ownership type of the compound. */
  int owns : 1;

  /* Set to non-zero, if data.parent is valid field containing Lua
     reference to parent object (i.e. owns memory to which `addr'
     points to). */
  int has_parent : 1;

  /* If the structure is allocated 'on the stack', its data is here. */
  union
  {
    gchar data[1];
    int parent;
  } data;
} Compound;
#define UD_COMPOUND "lgi.compound"

/* Loads reg_typeinfo and ref_repo elements for compound arg on the stack.  */
static Compound *
compound_prepare (lua_State *L, int arg, gboolean throw)
{
  /* Check metatable.  Don't use luaL_checkudata, because we want better type
     specified in the error message in case of type mismatch. */
  if (lua_getmetatable (L, arg))
    {
      lua_getfield (L, LUA_REGISTRYINDEX, UD_COMPOUND);
      int equal = lua_equal (L, -1, -2);
      lua_pop (L, 2);

      if (equal)
	{
	  /* Type is fine, clean metatables from stack and do real
	     preparation. */
	  Compound *compound = lua_touserdata (L, arg);
	  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
	  lua_rawgeti (L, -1, LGI_REG_TYPEINFO);
	  lua_replace (L, -2);
	  lua_rawgeti (L, -1, compound->ref_repo);
	  g_assert (!lua_isnil (L, -1));
	  return compound;
	}
    }

  /* Report error if requested. */
  if (throw)
    luaL_typerror (L, arg, "lgi.Object");

  return NULL;
}

static int
compound_register (lua_State *L, GIBaseInfo *info, gpointer *addr,
		   gboolean owns, int parent, gboolean alloc_struct)
{
  Compound *compound;
  gsize size;
  GIInfoType info_type;
  GType gtype, leaf_gtype;

  g_assert (addr != NULL);
  luaL_checkstack (L, 7, "");

  /* Convert 'parent' to absolute stack index. */
  if (parent < 0)
    parent += lua_gettop (L) + 1;

  /* Prepare access to registry and cache. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti (L, -1, LGI_REG_CACHE);

  /* NULL pointer results in 'nil' compound, unless 'allocate' is requested. */
  if (*addr == NULL)
    {
      if (!alloc_struct)
	{
	  lua_pushnil (L);
	  return 1;
	}
    }
  else
    {
      /* Check, whether the compound is not already in the cache. */
      lua_pushlightuserdata (L, *addr);
      lua_rawget (L, -2);
      if (!lua_isnil (L, -1))
	{
	  lua_replace (L, -3);
	  lua_pop (L, 1);
	  return 1;
	}
      else
	lua_pop (L, 1);
    }

  /* Create and initialize new userdata instance. */
  size = G_STRUCT_OFFSET (Compound, data);
  if (alloc_struct)
    {
      info_type = g_base_info_get_type (info);
      if (info_type == GI_INFO_TYPE_STRUCT)
	size += g_struct_info_get_size (info);
      else if (info_type == GI_INFO_TYPE_UNION)
	size += g_union_info_get_size (info);
    }
  else if (parent != 0)
    size = sizeof (Compound);
  compound = lua_newuserdata (L, size);
  compound->owns = 0;
  compound->has_parent = 0;
  compound->ref_repo = LUA_REFNIL;
  luaL_getmetatable (L, UD_COMPOUND);
  lua_setmetatable (L, -2);
  if (alloc_struct)
    {
      *addr = compound->data.data;
      memset (compound->data.data, 0, size - G_STRUCT_OFFSET (Compound, data));
    }
  else if (parent != 0)
    {
      compound->has_parent = 1;
      lua_pushvalue (L, parent);
      compound->data.parent = luaL_ref (L, LUA_REGISTRYINDEX);
    }

  /* Load ref_repo reference to repo table of the object.  This is a bit
     complicated, because we try to find the most specialized object for which
     we still have repo type installed, so that e.g. API marked as returning
     GObject, and returns instance of Gtk.Window returns object Gtk.Window
     without need to explicitely cast. */
  lua_rawgeti (L, -3, LGI_REG_REPO);
  gtype = g_registered_type_info_get_g_type (info);
  leaf_gtype = g_base_info_get_type (info) == GI_INFO_TYPE_OBJECT
    ? G_TYPE_FROM_INSTANCE (*addr) : gtype;
  g_base_info_ref (info);
  lua_pushnil (L);
  for (; gtype != G_TYPE_INVALID; gtype = g_type_next_base (leaf_gtype, gtype))
    {
      /* Try to find type in the repo. */
      if (info == NULL)
	info = g_irepository_find_by_gtype (NULL, gtype);
      if (G_LIKELY (info != NULL))
	{
	  lua_getfield (L, -2, g_base_info_get_namespace (info));
	  if (G_LIKELY (!lua_isnil (L, -1)))
	    {
	      lua_getfield (L, -1, g_base_info_get_name (info));
	      if (G_LIKELY (!lua_isnil (L, -1)))
		{
		  /* Replace the best result we've found so far. */
		  lua_replace (L, -3);
		  lua_pop (L, 1);
		}
	      else
		/* pop (namespace, nil) pair. */
		lua_pop (L, 2);
	    }
	  else
	    /* pop nil-namespace */
	    lua_pop (L, 1);

	  /* Reset info for the next round, if it will come. */
	  g_base_info_unref (info);
	  info = NULL;
	}
    }

  /* If we failed to find suitable type in the repo, fail. */
  if (G_UNLIKELY (lua_isnil (L, -1)))
    {
      lua_pop (L, 5);
      return 0;
    }

  /* Replace now unneded stack space of LGI_REG_REPO with found type. */
  lua_replace (L, -2);

  /* Store it to the typeinfo. */
  lua_rawgeti (L, -4, LGI_REG_TYPEINFO);
  lua_pushvalue (L, -2);
  compound->ref_repo = luaL_ref (L, -2);
  lua_pop (L, 2);
  compound->addr = *addr;
  compound->owns = owns;

  /* If we are storing owned gobject, make sure that we fully sink them.  We
     are not interested in floating refs. Note that there is ugly exception;
     GtkWindow's constructor returns non-floating object, but it keeps the
     reference to window internally, so we want acquire one extra reference. */
  if (owns && g_type_is_a (leaf_gtype, G_TYPE_OBJECT) &&
      (G_IS_INITIALLY_UNOWNED (*addr) || g_object_is_floating (*addr)))
      g_object_ref_sink (*addr);

  /* Store newly created compound to the cache. */
  lua_pushlightuserdata (L, compound->addr);
  lua_pushvalue (L, -2);
  lua_rawset (L, -4);

  /* Clean up the stack; we still have 'registry' and 'cache' tables above our
     requested compound, so remove them. */
  lua_replace (L, -3);
  lua_pop (L, 1);
  return 1;
}

int
lgi_compound_create (lua_State *L, GIBaseInfo *ii, gpointer addr, gboolean own,
		     int parent)
{
  return compound_register (L, ii, &addr, own, parent, FALSE);
}

gpointer
lgi_compound_struct_new (lua_State *L, GIBaseInfo *ii)
{
  /* Register struct, allocate space for it inside compound.  Mark is
     non-owned, because we do not need to free it in any way; its space will be
     reclaimed with compound itself. */
  gpointer addr = NULL;
  return compound_register (L, ii, &addr, FALSE, 0, TRUE) ? addr : NULL;
}

gpointer
lgi_compound_object_new (lua_State *L, GIObjectInfo *oi, int argtable)
{
  gint n_params = 0;
  GParameter *params = NULL, *param;
  GIPropertyInfo *pi;
  GITypeInfo *ti;
  gpointer addr;

  /* Check, whether 2nd argument is table containing construction-time
     properties. */
  if (!lua_isnoneornil (L, argtable))
    {
      luaL_checktype (L, argtable, LUA_TTABLE);

      /* Find out how many parameters we have. */
      lua_pushnil (L);
      for (lua_pushnil (L); lua_next (L, 2) != 0; lua_pop (L, 1))
	n_params++;

      if (n_params > 0)
	{
	  /* Allocate GParameter array (on the stack) and fill it
	     with parameters. */
	  param = params = g_newa (GParameter, n_params);
	  memset (params, 0, sizeof (GParameter) * n_params);
	  for (lua_pushnil (L); lua_next (L, 2) != 0; lua_pop (L, 1), param++)
	    {
	      /* Get property info. */
	      Compound *compound = lua_touserdata (L, -2);
	      if (G_UNLIKELY (compound == NULL ||
			      (pi = compound->addr) == NULL))
		{
		  lua_pushfstring (L, "bad ctor property for %s.%s",
				   g_base_info_get_namespace (oi),
				   g_base_info_get_name (oi));
		  luaL_argerror (L, argtable, lua_tostring (L, -1));
		}
	      else
		{
		  /* Extract property name. */
		  param->name = g_base_info_get_name (pi);

		  /* Initialize and load parameter value from the table
		     contents. */
		  ti = g_property_info_get_type (pi);
		  lgi_guard_create_baseinfo (L, ti);
		  g_value_init (&param->value, lgi_get_gtype (L, ti));
		  lgi_marshal_val_2c (L, ti, GI_TRANSFER_NOTHING,
				      &param->value, -2);
		  lua_pop (L, 1);
		}
	    }

	  /* Pop the repo class table. */
	  lua_pop (L, 1);
	}
    }

  /* Create the object. */
  addr = g_object_newv (g_registered_type_info_get_g_type (oi), n_params,
			params);

  /* Free all parameters from params array. */
  for (param = params; n_params > 0; param++, n_params--)
    g_value_unset (&param->value);

  /* And wrap a nice userdata around it. */
  if (addr == NULL)
    {
      lua_concat (L, lgi_type_get_name (L, oi));
      luaL_error (L, "failed to create instance of `%s'", lua_tostring (L, -1));
    }

  return compound_register (L, oi, &addr, TRUE, 0, FALSE) ? addr : NULL;
}

static int
compound_gc (lua_State *L)
{
  Compound *compound = compound_prepare (L, 1, TRUE);
  if (compound->owns)
    {
      /* Check the gtype of the compound. */
      GType gtype;
      lua_rawgeti (L, -1, 0);
      lua_getfield (L, -1, "gtype");
      gtype = lua_tointeger (L, -1);
      lua_pop (L, 2);

      /* Decide what to do according to the type. */
      if (G_TYPE_IS_OBJECT (gtype))
	g_object_unref (compound->addr);
      else if (G_TYPE_IS_BOXED (gtype))
	g_boxed_free (gtype, compound->addr);
      else
	{
	  /* Some other fundamental type, check, whether it has
	     registered custom unref method. */
	  GIObjectInfo *info = g_irepository_find_by_gtype (NULL, gtype);
	  if (info != NULL)
	    {
	      if (g_object_info_get_fundamental (info))
		{
		  GIObjectInfoUnrefFunction unref =
		    g_object_info_get_unref_function_pointer (info);
		  if (unref != NULL)
		    unref (compound->addr);
		}
	      g_base_info_unref (info);
	    }
	}
    }

  if (compound->has_parent)
    luaL_unref (L, LUA_REGISTRYINDEX, compound->data.parent);

  /* Free the reference to the repo object in typeinfo regtable. */
  luaL_unref (L, -2, compound->ref_repo);
  return 0;
}

static int
compound_tostring (lua_State *L)
{
  GIBaseInfo *bi;
  Compound *compound = compound_prepare (L, 1, TRUE);
  const char *type = "";

  /* Find out type of the compound. */
  lua_rawgeti (L, -1, 0);
  lua_getfield (L, -1, "gtype");
  bi = g_irepository_find_by_gtype (NULL, lua_tonumber (L, -1));
  if (bi != NULL)
    {
      switch (g_base_info_get_type (bi))
	{
	case GI_INFO_TYPE_OBJECT:
	  type = ".obj";
	  break;

	case GI_INFO_TYPE_INTERFACE:
	  type = ".ifc";
	  break;

	case GI_INFO_TYPE_STRUCT:
	  type = ".rec";
	  break;

	case GI_INFO_TYPE_UNION:
	  type = ".uni";
	  break;

	default:
	  break;
	}

      g_base_info_unref (bi);
    }
  lua_pop (L, 2);

  /* Create the whole name string. */
  lua_pushfstring (L, "lgi%s %p:", type, compound->addr);
  lua_rawgeti (L, -2, 0);
  lua_getfield (L, -1, "name");
  lua_replace (L, -2);
  lua_concat (L, 2);
  return 1;
}

static Compound *
compound_check (lua_State *L, int arg, GType *gtype)
{
  Compound *compound;
  GType real_type;

  /* First check whether we have directly compound userdata, and also
     check for type ancestry. */
  compound = compound_prepare (L, arg, FALSE);
  if (compound != NULL)
    {
      lua_rawgeti (L, -1, 0);
      lua_getfield (L, -1, "gtype");
      real_type = lua_tointeger (L, -1);
      lua_pop (L, 4);
      if (*gtype == G_TYPE_NONE || g_type_is_a (real_type, *gtype))
	{
	  *gtype = real_type;
	  return compound;
	}
    }

  return NULL;
}

gpointer
lgi_compound_check (lua_State *L, int arg, GType *gtype)
{
  Compound *compound = compound_check (L, arg, gtype);
  return compound != NULL ? compound->addr : NULL;
}

int
lgi_compound_get (lua_State *L, int index, GType *gtype, gpointer *addr,
		  unsigned flags)
{
  Compound *compound;
  int vals, gottype;
  GIBaseInfo *info;

  *addr = NULL;
  if (lua_isnoneornil (L, index))
    return (flags & LGI_FLAGS_OPTIONAL) ? 0 : luaL_argerror (L, index, "nil");

  /* Check compound type. */
  compound = compound_check (L, index, gtype);
  if (compound != NULL)
    {
      *addr = compound->addr;
      return 0;
    }

  /* Direct type value failed, so try to invoke explicit 'constructor'
     of the type, i.e. when attempting to create instance of Foo.Bar
     from param arg, call 'local inst = repo.Foo.Bar[0].construct(arg)'. */
  info = g_irepository_find_by_gtype (NULL, *gtype);
  if (*gtype != G_TYPE_NONE && info != NULL)
    {
      lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
      lua_rawgeti (L, -1, LGI_REG_REPO);
      lua_getfield (L, -1, g_base_info_get_namespace (info));
      vals = 3;
      if (!lua_isnil (L, -1))
	{
	  lua_getfield (L, -1, g_base_info_get_name (info));
	  vals++;
	  if (!lua_isnil (L, -1))
	    {
	      /* Get '[0].construct' part. */
	      lua_rawgeti (L, -1, 0);
	      vals++;
	      if (!lua_isnil (L, -1))
		{

		  lua_getfield (L, -1, "construct");
		  vals++;
		  if (!lua_isnil (L, -1))
		    {
		      /* Call the constructor. */
		      lua_pushvalue (L, index);
		      lua_call (L, 1, 1);

		      /* Return object returned by the constructor. */
		      lua_replace (L, -5);
		      lua_pop (L, 3);
		      vals = lgi_compound_get (L, -1, gtype, addr, flags) + 1;
		      if (*addr != NULL)
			{
			  g_base_info_unref (info);
			  return vals;
			}

		      lua_pop (L, vals);
		      vals = 0;
		    }
		}
	    }
	}

      lua_pop (L, vals);
    }

  /* Put exact requested type into the error message. */
  gottype = lua_type (L, index);
  if (info != NULL)
    {
      vals = lgi_type_get_name (L, info);
      g_base_info_unref (info);
    }
  else
    {
      lua_pushfstring (L, "(%s)", g_type_name (*gtype));
      vals = 1;
    }

  lua_pushstring (L, " expected, got ");
  lua_pushstring (L, lua_typename (L, gottype));
  lua_concat (L, vals + 2);
  luaL_argerror (L, index, lua_tostring (L, -1));
  return 0;
}

int
lgi_compound_properties(lua_State *L)
{
  int vals = 0;
  GIBaseInfo *pspec_info;
  gpointer obj;
  GType gtype = G_TYPE_OBJECT;
  gboolean list = lua_isnoneornil (L, 2);

  lua_pop (L, lgi_compound_get(L, 1, &gtype, &obj, 0));
  pspec_info = g_irepository_find_by_name (NULL, "GObject", "ParamSpec");
  lgi_guard_create_baseinfo (L, pspec_info);
  if (list)
    {
      /* List all properties of the object, store them into the table. */
      guint n_properties, i;
      GParamSpec **pspecs;

      lua_newtable (L);
      vals = 1;
      pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (obj),
					       &n_properties);
      for (i = 0; i < n_properties; ++i)
	if (lgi_compound_create (L, pspec_info, pspecs[i], FALSE, 0))
	  lua_setfield (L, -2, pspecs[i]->name);

      /* Returned array has to be freed. */
      g_free (pspecs);
    }
  else
    {
      /* Find specified property. */
      GParamSpec *pspec =
	g_object_class_find_property (G_OBJECT_GET_CLASS (obj),
				      lua_tostring (L, 2));
      if (pspec != NULL)
	vals = lgi_compound_create (L, pspec_info, pspec, FALSE, 0);
    }

  return vals;
}

/* Helper method, performs elementof() operation on property of given
   object. */
static int
compound_propertyof (lua_State *L, Compound *compound, GITypeInfo *ti,
		     GType gtype, const gchar *name, GParamFlags flags)
{
  int vals = 0;
  GValue val = {0};
  g_value_init (&val, gtype);

  if (!lua_toboolean (L, 3))
    {
      if ((flags & G_PARAM_READABLE) == 0)
	return luaL_argerror (L, 2, "not readable");

      g_object_get_property (compound->addr, name, &val);
      lgi_marshal_val_2lua (L, ti, GI_TRANSFER_NOTHING, &val);
      vals = 1;
    }
  else
    {
      if ((flags & G_PARAM_WRITABLE) == 0)
	return luaL_argerror (L, 2, "not writable");

      lgi_marshal_val_2c (L, ti, GI_TRANSFER_NOTHING, &val, 4);
      g_object_set_property (compound->addr, name, &val);
    }

  if (G_IS_VALUE (&val))
    g_value_unset (&val);

  return vals;
}

int
lgi_compound_elementof (lua_State *L)
{
  Compound *compound;
  GIBaseInfo *ei;
  GType gtype;
  int vals = 0;

  /* Prepare arg 1 - object on while we want access element. */
  compound = compound_prepare (L, 1, TRUE);

  /* Check argument 2; is it GIBaseInfo? */
  gtype = GI_TYPE_BASE_INFO;
  ei = lgi_compound_check (L, 2, &gtype);
  if (ei != NULL)
    {
      GITypeInfo *ti;
      int ti_guard;
      switch (g_base_info_get_type (ei))
	{
	case GI_INFO_TYPE_PROPERTY:
	  {
	    ti = g_property_info_get_type (ei);
	    ti_guard = lgi_guard_create_baseinfo (L, ti);
	    vals = compound_propertyof (L, compound, ti, lgi_get_gtype (L, ti),
					g_base_info_get_name (ei),
					g_property_info_get_flags (ei));
	    lua_remove (L, ti_guard);
	    break;
	  }

	case GI_INFO_TYPE_FIELD:
	  {
	    GIArgument *val = G_STRUCT_MEMBER_P (compound->addr,
						 g_field_info_get_offset (ei));
	    gint flags = g_field_info_get_flags (ei);

	    ti = g_field_info_get_type (ei);
	    ti_guard = lgi_guard_create_baseinfo (L, ti);

	    if (!lua_toboolean (L, 3))
	      {
		if ((flags & GI_FIELD_IS_READABLE) == 0)
		  return luaL_argerror (L, 2, "not readable");

		lgi_marshal_arg_2lua (L, ti, GI_TRANSFER_NOTHING, val, 1,
				      FALSE, NULL, NULL);
		vals = 1;
	      }
	    else
	      {
		if ((flags & GI_FIELD_IS_WRITABLE) == 0)
		  return luaL_argerror (L, 2, "not writable");

		lua_pop (L, lgi_marshal_arg_2c (L, ti, NULL,
						GI_TRANSFER_NOTHING,
						val, 4, FALSE, NULL, NULL));
		vals = 0;
	      }

	    lua_remove (L, ti_guard);
	    break;
	  }

	default:
	  g_assert_not_reached ();
	}

      /* Do not g_base_info_unref (ei), because it is owned by
	 compound passed as 2nd argument. */
    }
  else
    {
      /* Arg 2 is not GIBaseInfo, it might be a pspec. */
      GParamSpec *pspec;
      gtype = G_TYPE_PARAM;
      pspec = lgi_compound_check (L, 2, &gtype);
      if (pspec != NULL)
	vals = compound_propertyof (L, compound, NULL, pspec->value_type,
				    pspec->name, pspec->flags);
      else
	luaL_typerror (L, 2, "GIBaseInfo or GParamSpec");
    }

  return vals;
}

static int
compound_element (lua_State *L, gboolean write)
{
  int type;
  const gchar *error_message;

  /* Load compound and element. */
  compound_prepare (L, 1, TRUE);
  lua_getfield(L, -1, "_element");
  lua_pushvalue(L, -2);
  lua_pushvalue(L, 1);
  lua_pushvalue(L, 2);
  lua_call(L, 3, 1);
  type = lua_type (L, -1);

  if (type == LUA_TNIL)
    error_message = "%s: no `%s'";
  else if (write && type != LUA_TFUNCTION)
    error_message =  "%s: `%s' not writable";
  else
    return type;

  /* Prepare name of the compound. */
  lua_rawgeti (L, -3, 0);
  lua_getfield (L, -2, "name");
  return luaL_error (L, error_message, lua_tostring (L, -1),
		     lua_tostring (L, 2));
}

static int
compound_index (lua_State *L)
{
  if (compound_element (L, FALSE) == LUA_TFUNCTION)
    {
      /* Custom function, so call it. */
      lua_pushvalue (L, 1);
      lua_pushvalue (L, 2);
      lua_pushboolean (L, 0);
      lua_call (L, 3, 1);
    }

  return 1;
}

static int
compound_newindex (lua_State *L)
{
  compound_element (L, TRUE);
  lua_pushvalue (L, 1);
  lua_pushvalue (L, 2);
  lua_pushboolean (L, 1);
  lua_pushvalue (L, 3);
  lua_call (L, 4, 0);
  return 0;
}

static const struct luaL_reg compound_reg[] = {
  { "__gc", compound_gc },
  { "__tostring", compound_tostring },
  { "__index", compound_index },
  { "__newindex", compound_newindex },
  { NULL, NULL }
};

void
lgi_compound_init (lua_State *L)
{
  /* Register compound metatable. */
  luaL_newmetatable (L, UD_COMPOUND);
  luaL_register (L, NULL, compound_reg);
  lua_pop (L, 1);
}
