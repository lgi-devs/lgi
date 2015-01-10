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

/* lightuserdata key to registry, containing table representing weak
   cache of known objects. */
static int cache;

/* lightuserdata key to registry for metatable of objects. */
static int object_mt;

/* lightuserdata key to registry, containing 'env' table, which maps
   lightuserdata(obj-addr) -> obj-env-table. */
static int env;

/* Keys in 'env' table containing quark used as object's qdata for env
   and thread which is used from qdata destroy callback. */
enum {
  OBJECT_QDATA_ENV = 1,
  OBJECT_QDATA_THREAD
};

/* Structure stored in GObject's qdata at OBJECT_QDATA_ENV. */
typedef struct _ObjectData
{
  gpointer object;
  gpointer state_lock;
  lua_State *L;
} ObjectData;

/* lightuserdata key to registry, containing metatable for object env
   guard. */
static int env_mt;

/* Structure containing object_env_guard userdata. */
typedef struct _ObjectEnvGuard
{
  gpointer object;
  GQuark id;
} ObjectEnvGuard;

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

/* This is workaround method for broken
   g_object_info_get_*_function_pointer() in GI 1.32.0. (see
   https://bugzilla.gnome.org/show_bug.cgi?id=673282) */
gpointer
lgi_object_get_function_ptr (GIObjectInfo *info,
			     const gchar *(*getter)(GIObjectInfo *))
{
  gpointer func = NULL;
  g_base_info_ref (info);
  while (info != NULL)
    {
      GIBaseInfo *parent;
      const gchar *func_name;

      /* Try to get the name and the symbol. */
      func_name = getter (info);
      if (func_name && g_typelib_symbol (g_base_info_get_typelib (info),
					 func_name, &func))
	{
	  g_base_info_unref (info);
	  break;
	}

      /* Iterate to the parent info. */
      parent = g_object_info_get_parent (info);
      g_base_info_unref (info);
      info = parent;
    }

  return func;
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
object_refsink (lua_State *L, gpointer obj, gboolean no_sink)
{
  GType gtype = G_TYPE_FROM_INSTANCE (obj);
  if (G_TYPE_IS_OBJECT (gtype))
    {
      if (G_UNLIKELY (no_sink))
	g_object_ref (obj);
      else
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
	lgi_object_get_function_ptr (info, g_object_info_get_ref_function);
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
	lgi_object_get_function_ptr (info, g_object_info_get_unref_function);
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
  lua_getfenv (L, 1);
  if (lua_isnil (L, -1))
    lua_pushliteral (L, "<??\?>");
  else
    {
      lua_getfield (L, -1, "_tostring");
      if (!lua_isnil (L, -1))
        {
          lua_pushvalue (L, 1);
          lua_call (L, 1, 1);
          return 1;
        }
      lua_getfield (L, -2, "_name");
    }
  lua_pushfstring (L, "lgi.obj %p:%s(%s)", obj, lua_tostring (L, -1),
		   g_type_name (gtype));
  return 1;
}

gpointer
lgi_object_2c (lua_State *L, int narg, GType gtype, gboolean optional,
	       gboolean nothrow, gboolean transfer)
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

  if (transfer)
    object_refsink (L, obj, FALSE);

  return obj;
}

int
lgi_object_2lua (lua_State *L, gpointer obj, gboolean own, gboolean no_sink)
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
  lua_pushlightuserdata (L, &object_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);
  object_type (L, G_TYPE_FROM_INSTANCE (obj));
  lua_setfenv (L, -2);


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
    object_refsink (L, obj, no_sink);

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
  object_get (L, 1);
  lua_getfenv (L, 1);
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

static const char *const query_mode[] = { "addr", "repo", NULL };

/* Queries for assorted instance properties. Lua-side prototype:
   res = object.query(objectinstance, mode [, iface-gtype])
   Supported mode strings are:
   'repo':  returns repotable for this instance.
   'addr':  returns lightuserdata with pointer to the object. */
static int
object_query (lua_State *L)
{
  gpointer object = object_check (L, 1);
  if (object)
    {
      int mode = luaL_checkoption (L, 2, query_mode[0], query_mode);
      if (mode == 0)
	lua_pushlightuserdata (L, object);
      else
	lua_getfenv (L, 1);
      return 1;
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
  lua_getfenv (L, 1);
  return lgi_marshal_field (L, object, getmode, 1, 2, 3);
}

static void
object_data_destroy (gpointer user_data)
{
  ObjectData *data = user_data;
  lua_State *L = data->L;
  lgi_state_enter (data->state_lock);
  luaL_checkstack (L, 4, NULL);

  /* Release 'obj' entry from 'env' table. */
  lua_pushlightuserdata (L, &env);
  lua_rawget (L, LUA_REGISTRYINDEX);

  /* Deactivate env_destroy, to avoid double destruction. */
  lua_pushlightuserdata (L, data->object);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1))
    *(gpointer **) lua_touserdata (L, -1) = NULL;
  lua_pushlightuserdata (L, data->object);
  lua_pushnil (L);
  lua_rawset (L, -4);
  lua_pop (L, 2);

  /* Leave the context and destroy data structure. */
  lgi_state_leave (data->state_lock);
  g_free (data);
}

static int
object_env_guard_gc (lua_State *L)
{
  ObjectEnvGuard *guard = lua_touserdata (L, -1);
  g_free (g_object_steal_qdata (G_OBJECT (guard->object), guard->id));
  return 0;
}

/* Object environment table accessor.  Lua-side prototype:
   env = object.env(objectinstance) */
static int
object_env (lua_State *L)
{
  ObjectData *data;
  gpointer obj = object_get (L, 1);
  if (!G_IS_OBJECT (obj))
    /* Only GObject instances can have environment. */
    return 0;

  /* Lookup 'env' table. */
  lua_pushlightuserdata (L, &env);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_pushlightuserdata (L, obj);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1))
    /* Object's env table for the object is attached to the
       controlling userdata in the 'env' table. */
    lua_getfenv (L, -1);
  else
    {
      ObjectEnvGuard *guard;

      /* Create new table which will serve as an object env table. */
      lua_newtable (L);

      /* Create userdata guard, which disconnects env when the state
	 dies.  Attach the actual env table as env table to the guard
	 udata. */
      guard = lua_newuserdata (L, sizeof (ObjectEnvGuard));
      guard->object = obj;
      lua_rawgeti (L, -4, OBJECT_QDATA_ENV);
      guard->id = lua_tonumber (L, -1);
      lua_pop (L, 1);
      lua_pushvalue (L, -2);
      lua_setfenv (L, -2);

      /* Store it to the 'env' table. */
      lua_pushlightuserdata (L, obj);
      lua_pushvalue (L, -2);
      lua_rawset (L, -6);

      /* Create and fill new ObjectData structure, to attach it to
	 object's qdata. */
      data = g_new (ObjectData, 1);
      data->object = obj;
      lua_rawgeti (L, -4, OBJECT_QDATA_THREAD);
      data->L = lua_tothread (L, -1);
      data->state_lock = lgi_state_get_lock (data->L);

      /* Attach ObjectData to the object. */
      g_object_set_qdata_full (G_OBJECT (obj), guard->id,
			       data, object_data_destroy);
      lua_pop (L, 2);
    }

  return 1;
}

/* Creates new object.  Lua-side prototypes:
   res = object.new(luserdata-ptr[, already_own[, no_sink]])
   res = object.new(gtype, { GParameter }) */
static int
object_new (lua_State *L)
{
  if (lua_islightuserdata (L, 1))
    /* Create object from the given pointer. */
    return lgi_object_2lua (L, lua_touserdata (L, 1), lua_toboolean (L, 2),
			    lua_toboolean (L, 3));
  else
    {
      /* Normally Lua code uses GObject.Object.new(), which maps
	 directly to g_object_newv(), but for some reason GOI < 1.0 does
	 not export this method in the typelib. */

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
	  lua_pushnumber (L, i + 1);
	  lua_gettable (L, 2);
	  lgi_type_get_repotype (L, G_TYPE_INVALID, gparam_info);
	  lgi_record_2c (L, -2, &params[i], TRUE, FALSE, FALSE, FALSE);
	  lua_pop (L, 1);
	}

      /* Create the object and return it. */
      return lgi_object_2lua (L, g_object_newv (gtype, size, params),
			      TRUE, FALSE);
    }
}

/* Object API table. */
static const luaL_Reg object_api_reg[] = {
  { "query", object_query },
  { "field", object_field },
  { "new", object_new },
  { "env", object_env },
  { NULL, NULL }
};

void
lgi_object_init (lua_State *L)
{
  char *id;

  /* Register metatable. */
  lua_pushlightuserdata (L, &object_mt);
  lua_newtable (L);
  luaL_register (L, NULL, object_mt_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Initialize object cache. */
  lgi_cache_create (L, &cache, "v");

  /* Create table for 'env' tables. */
  lua_pushlightuserdata (L, &env);
  lua_newtable (L);

  /* Add OBJECT_QDATA_ENV quark to env table. */
  id = g_strdup_printf ("lgi:%p", L);
  lua_pushnumber (L, g_quark_from_string (id));
  g_free (id);
  lua_rawseti (L, -2, OBJECT_QDATA_ENV);

  /* Add OBJECT_QDATA_THREAD to env table. */
  lua_newthread (L);
  lua_rawseti (L, -2, OBJECT_QDATA_THREAD);

  /* Add 'env' table to the registry. */
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Register env_mt table. */
  lua_pushlightuserdata (L, &env_mt);
  lua_newtable (L);
  lua_pushcfunction (L, object_env_guard_gc);
  lua_setfield (L, -2, "__gc");
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Create object API table and set it to the parent. */
  lua_newtable (L);
  luaL_register (L, NULL, object_api_reg);
  lua_setfield (L, -2, "object");
}
