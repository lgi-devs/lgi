/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * GObject and GTypeInstance handling.
 */

#include "lgi.h"

/* Strong and weak cache.  Objects are always in weak cache, and
   added/removed from strong cache according to gobject's toggle_ref
   notifications. */
static int ref_weak_cache, ref_strong_cache;

/* Lua register ref for metatable of objects. */
static int object_mt_ref;

/* Checks that given narg is object type and returns pointer to type
   instance representing it. */
static gpointer
object_check (lua_State *L, int narg)
{
  gpointer *obj = lua_touserdata (L, narg);
  luaL_checkstack (L, 3, "");
  if (!lua_getmetatable (L, narg))
    return NULL;
  lua_rawgeti (L, LUA_REGISTRYINDEX, object_mt_ref);
  if (!lua_equal (L, -1, -2))
    obj = NULL;

  lua_pop (L, 2);
  g_assert (obj == NULL || *obj != NULL);
  return obj ? *obj : NULL;
}

/* Throws type error for object at given argument, gtype can
   optionally contain name of requested type. */
static int
object_type_error (lua_State *L, int narg, GType gtype)
{
  /* Look up type table for given object gtype or any of its
     predecessor (if available). */
  GType type_walker;
  luaL_checkstack (L, 5, "");
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_ref_repo);
  for (type_walker = gtype;;)
    {
      if (type_walker == G_TYPE_INVALID)
	{
	  if (gtype == G_TYPE_INVALID)
	    lua_pushliteral (L, "lgi.object");
	  else
	    lua_pushstring (L, g_type_name (gtype));
	  break;
	}

      /* Try to lookup table by gtype in repo. */
      lua_pushnumber (L, type_walker);
      lua_rawget (L, -2);
      if (!lua_isnil (L, -1))
	{
	  lua_getfield (L, -1, "_name");
	  lua_pushfstring (L, gtype == type_walker ? "%s" : "%s(%s)",
			   lua_tostring (L, -1), g_type_name (gtype));
	  break;
	}

      lua_pop (L, 1);
      type_walker = g_type_parent (type_walker);
    }

  /* Create error message. */
  lua_pushstring (L, lua_typename (L, lua_type (L, narg)));
  lua_pushfstring (L, "%s expected, got %s", lua_tostring (L, -2),
		   lua_tostring (L, -1));
  return luaL_argerror (L, narg, lua_tostring (L, -1));
}

static gpointer
object_get (lua_State *L, int narg)
{
  gpointer obj = object_get (L, narg);
  if (G_UNLIKELY (!obj))
    object_type_error (L, narg, G_TYPE_INVALID);
  return obj;
}

static int
object_gc (lua_State *L)
{
  gpointer obj = object_get (L, 1);
  GType gtype = G_TYPE_FROM_INSTANCE (obj);
  if (G_TYPE_IS_OBJECT (gtype))
    g_object_unref (obj);
  else
    {
      /* Some other fundamental type, check, whether it has
	 registered custom unref method. */
      GIObjectInfo *info = g_irepository_find_by_gtype (NULL, gtype);
      if (info != NULL)
	{
	  GIObjectInfoUnrefFunction unref;
	  if (g_object_info_get_fundamental (info)
	      && (unref = g_object_info_get_unref_function_pointer (info)))
	    unref (obj);
	  g_base_info_unref (info);
	}
    }

  return 0;
}

gpointer
lgi_object_2c (lua_State *L, int narg, GType gtype, gboolean optional)
{
  gpointer obj;

  g_return_val_if_fail (gtype != G_TYPE_INVALID, NULL);

  /* Check for nil. */
  if (optional && lua_isnoneornil (L, narg))
    return NULL;

  /* Get instance and perform type check. */
  obj = object_check (L, narg);
  if (!obj || !g_type_is_a (G_TYPE_FROM_INSTANCE (obj), gtype))
    object_type_error (L, narg, gtype);

  return obj;
}

/* Registration table. */
static const luaL_Reg object_mt_reg[] = {
  { "__gc", object_gc },
  { NULL, NULL }
};

void
lgi_object_init (lua_State *L)
{
  /* Register metatable. */
  lua_newtable (L);
  luaL_register (L, NULL, object_mt_reg);
  object_mt_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Initialize caches. */
  ref_weak_cache = lgi_create_cache (L, "v");
  ref_strong_cache = lgi_create_cache (L, NULL);
}
