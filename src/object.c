/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * GObject and GTypeInstance handling.
 */

#include <string.h>
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
  lua_rawget (L, LUA_REGISTRYINDEX);
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

  /* NULL pointer results in nil. */
  if (!obj)
    {
      lua_pushnil (L);
      return;
    }

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

/* Creation of specified object with given construction-time properties.
   obj = object.new(gtype[, {gi.prop->val}]) */
static int
object_new (lua_State *L)
{
  gint n_params = 0;
  GParameter *params = NULL, *param;
  GIPropertyInfo *pi;
  GITypeInfo *ti;
  gpointer obj;
  GType gtype = luaL_checknumber (L, 1);

  g_assert (G_TYPE_IS_OBJECT (gtype));

  /* Go through the table and extract and prepare
     construction-time properties. */
  luaL_checktype (L, 2, LUA_TTABLE);

  /* Find out how many parameters we have. */
  lua_pushnil (L);
  for (lua_pushnil (L); lua_next (L, 2) != 0; lua_pop (L, 1))
    n_params++;

  if (n_params > 0)
    {
      /* Allocate GParameter array (on the stack) and fill it with
	 parameters. */
      param = params = g_newa (GParameter, n_params);
      memset (params, 0, sizeof (GParameter) * n_params);
      for (lua_pushnil (L); lua_next (L, 2) != 0; lua_pop (L, 1), param++)
	{
	  /* Get property info. */
	  pi = *(GIBaseInfo **) luaL_checkudata (L, -2, LGI_GI_INFO);

	  /* Extract property name. */
	  param->name = g_base_info_get_name (pi);

	  /* Initialize and load parameter value from the table
	     contents. */
	  ti = g_property_info_get_type (pi);
	  lgi_gi_info_new (L, ti);
	  g_value_init (&param->value, lgi_get_gtype (L, ti));
	  lgi_marshal_val_2c (L, ti, GI_TRANSFER_NOTHING, &param->value, -2);
	  lua_pop (L, 1);
	}

      /* Pop the repo class table. */
      lua_pop (L, 1);
    }

  /* Create the object. */
  obj = g_object_newv (gtype, n_params, params);

  /* Free all parameters from params array. */
  for (param = params; n_params > 0; param++, n_params--)
    g_value_unset (&param->value);

  /* Wrap Lua proxy around created object. */
  lgi_object_2lua (L, obj, TRUE);
  return 1;
}

/* Checks whether given value is object and returns its real gtype and
   associated type table. */
static int
object_typeof (lua_State *L)
{
  gpointer object = object_check (L, 1);
  if (object)
    {
      GType gtype = G_TYPE_FROM_INSTANCE (object);
      if (object_type (L, gtype) != G_TYPE_INVALID)
	{
	  lua_pushnumber (L, gtype);
	  return 2;
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

/* Object property accessor.  Lua-side prototypes:
   res = object.property(objectinstance, propref)
   object.property(objectinstance, propref, newvalue)
   where 'propref' is either gi.info of the property or record with
   ParamSpec returned by object.properties() call. */
static int
object_property (lua_State *L)
{
  int vals = 0;
  GValue val = {0};
  GType gtype;
  GITypeInfo *ti = NULL;
  GParamFlags flags;
  const gchar *name;

  /* Check, whether we are doing set or get operation. */
  gboolean getmode = lua_isnone (L, 3);

  /* Get object instance. */
  gpointer object = lgi_object_2c (L, 1, G_TYPE_OBJECT, FALSE);
  GIPropertyInfo *pi = lgi_gi_info_test (L, 2);
  if (pi)
    {
      ti = g_property_info_get_type (pi);
      lgi_gi_info_new (L, ti);
      gtype = lgi_get_gtype (L, ti);
      flags = g_property_info_get_flags (pi);
      name = g_base_info_get_name (pi);
    }
  else
    {
      /* If not gi.info, it must be record with pspec. */
      GParamSpec *pspec;
      GIBaseInfo *pspec_info =
	g_irepository_find_by_name (NULL, "GObject", "ParamSpec");
      lgi_gi_info_new (L, pspec_info);
      if (lgi_record_2c (L, pspec_info, 2, (gpointer *) &pspec, FALSE) > 0)
	g_assert_not_reached ();
      gtype = pspec->value_type;
      flags = pspec->flags;
      name = pspec->name;
    }

  /* Check access control. */
  if ((flags & (getmode ? G_PARAM_READABLE : G_PARAM_WRITABLE)) == 0)
    {
      /* Prepare proper error message. */
      object_type (L, G_OBJECT_TYPE (object));
      lua_getfield (L, -1, "_name");
      return luaL_error (L, "%s: property `%s' is not %s",
			 lua_tostring (L, -1), name,
			 getmode ? "readable" : "writable");
    }

  /* Initialize worker value with correct type. */
  g_value_init (&val, gtype);
  if (getmode)
    {
      g_object_get_property (object, name, &val);
      lgi_marshal_val_2lua (L, ti, GI_TRANSFER_NOTHING, &val);
      vals = 1;
    }
  else
    {
      lgi_marshal_val_2c (L, ti, GI_TRANSFER_NOTHING, &val, 3);
      g_object_set_property (object, name, &val);
    }

  g_value_unset (&val);
  return vals;
}

/* Lists all dynamic properties of given object. Lua-side prototype:
   {name->record(ParamSpec)} = object.properties(objectinstance)
   record(ParamSpec) = object.properties(objectinstance, name) */
static int
object_properties (lua_State *L)
{
  GIBaseInfo *pspec_info;
  GObjectClass *klass;
  gboolean list = lua_isnoneornil (L, 2);

  klass = G_OBJECT_GET_CLASS (lgi_object_2c (L, 1, G_TYPE_OBJECT, FALSE));
  pspec_info = g_irepository_find_by_name (NULL, "GObject", "ParamSpec");
  lgi_gi_info_new (L, pspec_info);
  if (list)
    {
      /* List all properties of the object, store them into the table. */
      guint n_properties, i;
      GParamSpec **pspecs;

      lua_newtable (L);
      pspecs = g_object_class_list_properties (klass, &n_properties);
      for (i = 0; i < n_properties; ++i)
	{
	  lgi_record_2lua (L, pspec_info, pspecs[i], LGI_RECORD_OWN, 0);
	  lua_setfield (L, -2, pspecs[i]->name);
	}

      /* Returned array has to be freed. */
      g_free (pspecs);
    }
  else
    {
      /* Find specified property. */
      GParamSpec *pspec =
	g_object_class_find_property (klass, lua_tostring (L, 2));
      if (pspec != NULL)
	lgi_record_2lua (L, pspec_info, pspec, LGI_RECORD_OWN, 0);
      else
	lua_pushnil (L);
    }

  return 1;
}

/* Returns table of interfaces implemented by given compound, exports
   them as gi.info(GIInterfaceInfo) instances. */
static int
object_interfaces(lua_State *L)
{
  guint n_interfaces, i, pos;
  GType *interfaces, gtype = G_OBJECT_TYPE (object_get (L, 1));

  /* Create table with resulting interfaces. */
  lua_newtable (L);
  interfaces = g_type_interfaces (gtype, &n_interfaces);
  for (i = 0, pos = 1; i < n_interfaces; i++)
    if (lgi_gi_info_new (L, g_irepository_find_by_gtype (NULL, interfaces[i])))
      lua_rawseti (L, -2, pos++);

  return 1;
}

/* Returns internal custom object's table, free to store/load anything
   into it. Lua-side prototype:
   env_table = object.env(objectinstance) */
static int
object_env (lua_State *L)
{
  object_get (L, 1);
  lua_getfenv (L, 1);
  return 1;
}

static void
gclosure_destroy (gpointer user_data, GClosure *closure)
{
  lgi_closure_destroy (user_data);
}

/* Connects signal to given compound.
 * Signature is:
 * handler_id = object.connect(obj, signame, callable, func, detail, after) */
static int
object_connect (lua_State *L)
{
  GObject *object;
  const char *signame = luaL_checkstring (L, 2);
  GICallableInfo *ci;
  const char *detail = lua_tostring (L, 5);
  gpointer call_addr, lgi_closure;
  GClosure *gclosure;
  guint signal_id;
  gulong handler_id;

  /* Get target objects. */
  object = lgi_object_2c (L, 1, G_TYPE_OBJECT, FALSE);
  ci = *(GIBaseInfo **) luaL_checkudata (L, 3, LGI_GI_INFO);

  /* Create GClosure instance to be used.  This is fast'n'dirty method; it
     requires less lines of code to write, but a lot of code to execute when
     the signal is emitted; the signal goes like this:

     1) emitter prepares params as an array of GValues.
     2) GLib's marshaller converts it to C function call.
     3) this call lands in libffi's trampoline (closure)
     4) this trampoline converts arguments to libffi array of args
     5) LGI libffi glue code unmarshalls them to Lua stack and calls Lua func.

     much better solution would be writing custom GClosure Lua marshaller, in
     which case the scenraio would be following:

     1) emitter prepares params as an array of GValues.
     2) LGI custom marshaller marshalls then to Lua stack and calls Lua
	function. */
  lgi_closure = lgi_closure_create (L, ci, 4, FALSE, &call_addr);
  gclosure = g_cclosure_new (call_addr, lgi_closure, gclosure_destroy);

  /* Connect closure to the signal. */
  signal_id = g_signal_lookup (signame, G_OBJECT_TYPE (object));
  handler_id =  g_signal_connect_closure_by_id (object, signal_id,
						g_quark_from_string (detail),
						gclosure, lua_toboolean (L, 6));
  lua_pushnumber (L, handler_id);
  return 1;
}

/* Object API table. */
static const luaL_Reg object_api_reg[] = {
  { "new", object_new },
  { "typeof", object_typeof },
  { "field", object_field },
  { "property", object_property },
  { "properties", object_properties },
  { "interfaces", object_interfaces },
  { "env", object_env },
  { "connect", object_connect },
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

  /* Create object API table and set it to the parent. */
  lua_newtable (L);
  luaL_register (L, NULL, object_api_reg);
  lua_setfield (L, -2, "object");
}
