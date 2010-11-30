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

/* lightuserdata keys to registry, containing tables representing
   strong and weak caches.  Objects are always in weak cache, and
   added/removed from strong cache according to gobject's toggle_ref
   notifications. */
static int cache_weak, cache_strong;

/* lightuserdata key to registry containing thread to be used for
   toggle reference callback. */
static int callback_thread;

/* lightuserdata key to registry for metatable of objects. */
static int object_mt;

/* Checks that given narg is object type and returns pointer to type
   instance representing it. */
static gpointer
object_check (lua_State *L, int narg)
{
  gpointer *obj = lua_touserdata (L, narg);
  luaL_checkstack (L, 3, "");
  if (!lua_getmetatable (L, narg))
    return NULL;
  lua_pushlightuserdata (L, &object_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  if (!lua_equal (L, -1, -2))
    obj = NULL;

  lua_pop (L, 2);
  g_assert (obj == NULL || *obj != NULL);
  return obj ? *obj : NULL;
}

/* Walks given type and tries to find the closest known match of the
   object present in the repo. If found, leaves found type table on
   the stack and returns real found gtype, otherwise returns
   G_TYPE_INVALID. */
static GType
object_type (lua_State *L, GType gtype)
{
  luaL_checkstack (L, 2, "");
  lua_pushlightuserdata (L, &lgi_addr_repo);
  for (; gtype != G_TYPE_INVALID; gtype = g_type_parent (gtype))
    {
      /* Try to find type in the repo table. */
      lua_pushnumber (L, gtype);
      lua_rawget (L, -2);
      if (!lua_isnil (L, -1))
	{
	  lua_replace (L, -2);
	  return gtype;
	}
      else
	lua_pop (L, 1);
    }

  /* Not found, remove repo table from the stack. */
  lua_pop (L, 1);
  return G_TYPE_INVALID;
}

/* Throws type error for object at given argument, gtype can
   optionally contain name of requested type. */
static int
object_type_error (lua_State *L, int narg, GType gtype)
{
  GType found_gtype;
  /* Look up type table and get name from it. */
  luaL_checkstack (L, 4, "");
  found_gtype = object_type (L, gtype);
  if (found_gtype != G_TYPE_INVALID)
    {
      lua_getfield (L, -1, "_name");
      lua_pushfstring (L, gtype == found_gtype ? "%s" : "%s(%s)",
		       lua_tostring (L, -1), g_type_name (gtype));
    }
  else
    {
      if (gtype == G_TYPE_INVALID)
	lua_pushliteral (L, "lgi.object");
      else
	lua_pushstring (L, g_type_name (gtype));
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
  gpointer obj = object_check (L, narg);
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

/* GObject toggle-ref notification callback.  Inserts or removes given
   object from/to strong reference cache. */
static void
object_toggle_notify (gpointer data, GObject *object, gboolean is_last_ref)
{
  lua_State *L = data;
  luaL_checkstack (L, 3, "");
  lua_pushlightuserdata (L, &cache_strong);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_pushlightuserdata (L, object);
  if (is_last_ref)
    {
      /* Remove from strong cache (i.e. assign nil to that slot). */
      lua_pushnil (L);
    }
  else
    {
      /* Find proxy object in the weak table and assign it to the
	 strong table. */
      lua_pushlightuserdata (L, &cache_weak);
      lua_rawget (L, LUA_REGISTRYINDEX);
      lua_pushvalue (L, -2);
      lua_rawget (L, -2);
      lua_replace (L, -2);
    }

  /* Store new value to the strong cache. */
  lua_rawset (L, -3);
  lua_pop (L, 1);
}

void
lgi_object_2lua (lua_State *L, gpointer obj, gboolean own)
{
  GType gtype;

  /* Check, whether the object is already created (in the cache). */
  luaL_checkstack (L, 6, "");
  lua_pushlightuserdata (L, &cache_weak);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_pushlightuserdata (L, obj);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1))
    {
      /* Use the object from the cache. */
      lua_replace (L, -2);
      return;
    }

  /* Create new userdata object and attach empty table as its environment. */
  *(gpointer *) lua_newuserdata (L, sizeof (obj)) = obj;
  lua_pushlightuserdata (L, &object_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);
  lua_newtable (L);
  lua_setfenv (L, -2);

  /* Store newly created userdata proxy into weak cache. */
  lua_pushlightuserdata (L, obj);
  lua_pushvalue (L, -2);
  lua_rawset (L, -5);

  /* Stack cleanup, remove unnecessary weak cache and nil under userdata. */
  lua_replace (L, -3);
  lua_pop (L, 1);

  gtype = G_TYPE_FROM_INSTANCE (obj);
  if (G_TYPE_IS_OBJECT (gtype))
    {
      /* Make sure that floating and/or initially-unowned objects are
	 converted to regular reference; we are not intereseted in
	 floating refs, they just complicate stuff for us. */
      if (g_type_is_a (gtype, G_TYPE_INITIALLY_UNOWNED)
	  || g_object_is_floating (obj))
	g_object_ref_sink (obj);

      /* Create toggle reference and add object to the strong cache. */
      lua_pushlightuserdata (L, &callback_thread);
      lua_rawget (L, LUA_REGISTRYINDEX);
      g_object_add_toggle_ref (obj, object_toggle_notify, lua_tothread (L, -1));
      object_toggle_notify (L, obj, FALSE);
      lua_pop (L, 1);

      /* If the object was already pre-owned, remove one reference
	 (because we have one owning toggle reference). */
      if (own)
	g_object_unref (obj);
    }
  else if (!own)
    {
      /* Unowned fundamental non-GObject, try to get its ownership. */
      GIObjectInfo *info = g_irepository_find_by_gtype (NULL, gtype);
      if (info != NULL)
	{
	  GIObjectInfoRefFunction ref;
	  if (g_object_info_get_fundamental (info)
	      && (ref = g_object_info_get_ref_function_pointer (info)))
	    ref (obj);
	  g_base_info_unref (info);
	}
    }
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
  lua_pushlightuserdata (L, &object_mt);
  lua_newtable (L);
  luaL_register (L, NULL, object_mt_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Initialize caches. */
  lgi_cache_create (L, &cache_weak, "v");
  lgi_cache_create (L, &cache_strong, NULL);

  /* Create new service helper thread which is used solely for
     invoking toggle reference notification. */
  lua_pushlightuserdata (L, &callback_thread);
  lua_newthread (L);
  lua_rawset (L, LUA_REGISTRYINDEX);
}
