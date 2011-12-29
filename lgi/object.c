/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * GObject and GTypeInstance handling.
 */

#include <string.h>
#include "lgi.h"

/* lightuserdata keys to registry, containing table representing weak
   cache of known objects. */
static int cache;

static const luaL_Reg object_mt_reg[];

/* Checks that given narg is object type and returns pointer to type
   instance representing it. */
static gpointer
object_check (lua_State *L, int narg)
{
  gpointer *obj = lua_touserdata (L, narg);
  luaL_checkstack (L, 3, "");
  if (!lua_getmetatable (L, narg))
    return NULL;
  lua_pushlightuserdata (L, (void *) object_mt_reg);
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
  for (; gtype != G_TYPE_INVALID; gtype = g_type_parent (gtype))
    {
      /* Get appropriate repo table, if present. */
      lgi_type_get_repotype (L, gtype, NULL);
      if (!lua_isnil (L, -1))
	break;

      lua_pop (L, 1);
    }

  return gtype;
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

/* Retrieves requested typetable function for the object. */
static gpointer
object_load_function (lua_State *L, GType gtype, const gchar *name)
{
  gpointer func = NULL;
  if (object_type (L, gtype) != G_TYPE_INVALID)
    {
      func = lgi_gi_load_function (L, -1, name);
      lua_pop (L, 1);
    }
  return func;
}

/* Adds one reference to the object, returns TRUE if succeded. */
static gboolean
object_refsink (lua_State *L, gpointer obj)
{
  GType gtype = G_TYPE_FROM_INSTANCE (obj);
  if (G_TYPE_IS_OBJECT (gtype))
    {
      g_object_ref_sink (obj);
      return TRUE;
    }

  /* Check whether object has registered fundamental 'ref'
     function. */
  GIObjectInfo *info = g_irepository_find_by_gtype (NULL, gtype);
  if (info == NULL)
    info = g_irepository_find_by_gtype (NULL, G_TYPE_FUNDAMENTAL (gtype));
  if (info != NULL && g_object_info_get_fundamental (info))
    {
      GIObjectInfoRefFunction ref =
	g_object_info_get_ref_function_pointer (info);
      g_base_info_unref (info);
      if (ref != NULL)
	{
	  ref (obj);
	  return TRUE;
	}
    }

  /* Finally check custom _refsink method in typetable. */
  gpointer (*refsink_func)(gpointer) =
    object_load_function (L, gtype, "_refsink");
  if (refsink_func)
    {
      refsink_func (obj);
      return TRUE;
    }

  /* There is no known wasy how to ref this kind of object.  But this
     typically appears when handling GParamSpec, and GParamSpec
     handling generally works fine even without ref/unref, so the
     warnings produced are generally junk, so disabled until a way to
     handle ParamSpec properly is found. */
#if 0
  g_warning ("no way to ref type `%s'", g_type_name (gtype));
#endif
  return FALSE;
}

/* Removes one reference from the object. */
static void
object_unref (lua_State *L, gpointer obj)
{
  GType gtype = G_TYPE_FROM_INSTANCE (obj);
  if (G_TYPE_IS_OBJECT (gtype))
    {
      g_object_unref (obj);
      return;
    }

  /* Some other fundamental type, check, whether it has registered
     custom unref method. */
  GIObjectInfo *info = g_irepository_find_by_gtype (NULL, gtype);
  if (info == NULL)
    info = g_irepository_find_by_gtype (NULL, G_TYPE_FUNDAMENTAL (gtype));
  if (info != NULL && g_object_info_get_fundamental (info))
    {
      GIObjectInfoUnrefFunction unref =
	g_object_info_get_unref_function_pointer (info);
      g_base_info_unref (info);
      if (unref != NULL)
	{
	  unref (obj);
	  return;
	}
    }

  void (*unref_func)(gpointer) = object_load_function (L, gtype, "_unref");
  if (unref_func)
    {
      unref_func (obj);
      return;
    }

#if 0
  g_warning ("no way to unref type `%s'", g_type_name (gtype));
#endif
}

static int
object_gc (lua_State *L)
{
  object_unref (L, object_get (L, 1));
  return 0;
}

static int
object_tostring (lua_State *L)
{
  gpointer obj = object_get (L, 1);
  GType gtype = G_TYPE_FROM_INSTANCE (obj);
  if (object_type (L, gtype) != G_TYPE_INVALID)
    lua_getfield (L, -1, "_name");
  else
    lua_pushliteral (L, "<??\?>");
  lua_pushfstring (L, "lgi.obj %p:%s(%s)", obj, lua_tostring (L, -1),
		   g_type_name (gtype));
  return 1;
}

gpointer
lgi_object_2c (lua_State *L, int narg, GType gtype, gboolean optional,
	       gboolean nothrow)
{
  gpointer obj;

  /* Check for nil. */
  if (optional && lua_isnoneornil (L, narg))
    return NULL;

  /* Get instance and perform type check. */
  obj = object_check (L, narg);
  if (!nothrow
      && (!obj || (gtype != G_TYPE_INVALID
		   && !g_type_is_a (G_TYPE_FROM_INSTANCE (obj), gtype))))
    object_type_error (L, narg, gtype);

  return obj;
}

int
lgi_object_2lua (lua_State *L, gpointer obj, gboolean own)
{
  /* NULL pointer results in nil. */
  if (!obj)
    {
      lua_pushnil (L);
      return 1;
    }

  /* Check, whether the object is already created (in the cache). */
  luaL_checkstack (L, 6, "");
  lua_pushlightuserdata (L, &cache);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_pushlightuserdata (L, obj);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1))
    {
      /* Use the object from the cache. */
      lua_replace (L, -2);

      /* If the object was already owned, remove one reference,
	 because our proxy always keeps only one reference, which we
	 already have. */
      if (own)
	object_unref (L, obj);
      return 1;
    }

  /* Create new userdata object. */
  *(gpointer *) lua_newuserdata (L, sizeof (obj)) = obj;
  lua_pushlightuserdata (L, (void *) object_mt_reg);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);

  /* Store newly created userdata proxy into cache. */
  lua_pushlightuserdata (L, obj);
  lua_pushvalue (L, -2);
  lua_rawset (L, -5);

  /* Stack cleanup, remove unnecessary cache and nil under userdata. */
  lua_replace (L, -3);
  lua_pop (L, 1);

  /* If we don't own the object, take its ownership (and also remove
     floating reference if there is any). */
  if (!own)
    object_refsink (L, obj);

  return 1;
}

/* Worker method for __index and __newindex implementation. */
static int
object_access (lua_State *L)
{
  gboolean getmode = lua_isnone (L, 3);

  /* Check that 1st arg is an object and invoke one of the forms:
     result = type:_access(objectinstance, name)
     type:_access(objectinstance, name, val) */
  gpointer object = object_get (L, 1);
  GType gtype = G_TYPE_FROM_INSTANCE (object);
  if (object_type (L, gtype) == G_TYPE_INVALID)
    object_type_error (L, 1, gtype);
  return lgi_marshal_access (L, getmode, 1, 2, 3);
}

/* Registration table. */
static const luaL_Reg object_mt_reg[] = {
  { "__gc", object_gc },
  { "__tostring", object_tostring },
  { "__index", object_access },
  { "__newindex", object_access },
  { NULL, NULL }
};

static int
object_guard_gc (lua_State *L)
{
  gpointer obj = lua_touserdata (L, 1);
  if (obj != NULL)
    object_unref (L, obj);
  return 0;
}

/* Registration table. */
static const luaL_Reg object_guard_reg[] = {
  { "__gc", object_guard_gc },
  { NULL, NULL }
};
void
lgi_object_ref (lua_State *L, gpointer obj)
{
  luaL_checkstack (L, 2, NULL);
  if (obj != NULL && object_refsink (L, obj))
    {
      /* Create guard which will inref the object. */
      *(gpointer *) lua_newuserdata (L, sizeof (gpointer)) = obj;
      lua_pushlightuserdata (L, (void *) object_guard_reg);
      lua_gettable (L, LUA_REGISTRYINDEX);
      lua_setmetatable (L, -2);
    }
  else
    lua_pushnil (L);
}

static const char *const query_mode[] = {
  "gtype", "repo", "class", NULL
 };

/* Queries for assorted instance properties. Lua-side prototype:
   res = object.query(objectinstance, mode [, iface-gtype])
   Supported mode strings are:
   'gtype': returns real gtype of this instance.
   'repo':  returns repotable for this instance.
   'class': returns class struct record of this instance. */
static int
object_query (lua_State *L)
{
  gpointer object = object_check (L, 1);
  if (object)
    {
      int mode = luaL_checkoption (L, 2, query_mode[0], query_mode);
      GType gtype = lgi_type_get_gtype (L, 3);
      if (gtype == G_TYPE_INVALID)
	gtype = G_TYPE_FROM_INSTANCE (object);
      if (mode == 0)
	{
	  lua_pushnumber (L, gtype);
	  return 1;
	}
      else
	{
	  /* Get repotype structure. */
	  if (object_type (L, gtype) != G_TYPE_INVALID)
	    {
	      if (mode == 2)
		{
		  gpointer typestruct = !G_TYPE_IS_INTERFACE (gtype)
		    ? G_TYPE_INSTANCE_GET_CLASS (object, gtype, GTypeClass)
		    : G_TYPE_INSTANCE_GET_INTERFACE (object, gtype, GTypeClass);
		  lua_getfield (L, -1, "_class");
		  lgi_record_2lua (L, typestruct, FALSE, 0);
		}
	      return 1;
	    }
	}
    }
  return 0;
}

/* Object field accessor.  Lua-side prototypes:
   res = object.field(objectinstance, gi.fieldinfo)
   object.field(objectinstance, gi.fieldinfo, newvalue) */
static int
object_field (lua_State *L)
{
  /* Check, whether we are doing set or get operation. */
  gboolean getmode = lua_isnone (L, 3);

  /* Get object instance. */
  gpointer object = object_get (L, 1);

  /* Call field marshalling worker. */
  return lgi_marshal_field (L, object, getmode, 1, 2, 3);
}

/* Object creator.  Normally Lua code uses GObject.Object.new(), which
   maps directly to g_object_newv(), but for some reason GOI < 1.0
   does not export this method in the typelib. */
static int
object_new (lua_State *L)
{
  /* Get GType - 1st argument. */
  GParameter *params;
  size_t size, i;
  GIBaseInfo *gparam_info;
  GType gtype = lgi_type_get_gtype (L, 1);
  luaL_checktype (L, 2, LUA_TTABLE);

  /* Find BaseInfo of GParameter. */
  gparam_info = g_irepository_find_by_name (NULL, "GObject", "Parameter");
  *lgi_guard_create (L, (GDestroyNotify) g_base_info_unref) = gparam_info;

  /* Prepare array of GParameter structures. */
  size = lua_objlen (L, 2);
  params = g_newa (GParameter, size);
  for (i = 0; i < size; ++i)
    {
      lua_pushinteger (L, i + 1);
      lua_gettable (L, 2);
      lgi_type_get_repotype (L, G_TYPE_INVALID, gparam_info);
      memcpy (&params[i], lgi_record_2c (L, -2, FALSE, FALSE),
	      sizeof (GParameter));
      lua_pop (L, 1);
    }

  /* Create the object and return it. */
  return lgi_object_2lua (L, g_object_newv (gtype, size, params), TRUE);
}

/* Object API table. */
static const luaL_Reg object_api_reg[] = {
  { "query", object_query },
  { "field", object_field },
  { "new", object_new },
  { NULL, NULL }
};

void
lgi_object_init (lua_State *L)
{
  /* Register object metatable. */
  lua_pushlightuserdata (L, (void *) object_mt_reg);
  lua_newtable (L);
  luaL_register (L, NULL, object_mt_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Register object guard metatable. */
  lua_pushlightuserdata (L, (void *) object_guard_reg);
  lua_newtable (L);
  luaL_register (L, NULL, object_guard_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Initialize object cache. */
  lgi_cache_create (L, &cache, "v");

  /* Create object API table and set it to the parent. */
  lua_newtable (L);
  luaL_register (L, NULL, object_api_reg);
  lua_setfield (L, -2, "object");
}
