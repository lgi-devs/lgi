/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Core C utility API.
 */

#include <string.h>
#include "lgi.h"

#ifndef NDEBUG
const char *lgi_sd (lua_State *L)
{
  int i;
  static gchar *msg = 0;
  g_free (msg);
  msg = g_strdup ("");
  int top = lua_gettop (L);
  for (i = 1; i <= top; i++)
    {
      int t = lua_type (L, i);
      gchar *item, *nmsg;
      switch (t)
	{
	case LUA_TSTRING:
	  item = g_strdup_printf ("`%s'", lua_tostring (L, i));
	  break;

	case LUA_TBOOLEAN:
	  item = g_strdup_printf (lua_toboolean (L, i) ? "true" : "false");
	  break;

	case LUA_TNUMBER:
	  item = g_strdup_printf ("%g", lua_tonumber (L, i));
	  break;

	default:
	  item = g_strdup_printf ("%s(%p)", lua_typename (L, t),
				  lua_topointer (L, i));
	  break;
	}
      nmsg = g_strconcat (msg, " ", item, NULL);
      g_free (msg);
      g_free (item);
      msg = nmsg;
    }
  return msg;
}
#endif

void
lgi_cache_create (lua_State *L, gpointer key, const char *mode)
{
  lua_pushlightuserdata (L, key);
  lua_newtable (L);
  if (mode)
    {
      lua_newtable (L);
      lua_pushstring (L, mode);
      lua_setfield (L, -2, "__mode");
      lua_setmetatable (L, -2);
    }
  lua_rawset (L, LUA_REGISTRYINDEX);
}

static int core_addr_logger;
static int core_addr_getgtype;

static int
core_set(lua_State *L)
{
  const char *name = luaL_checkstring (L, 1);
  int *key;
  if (strcmp (name, "logger") == 0)
    key = &core_addr_logger;
  else if (strcmp (name, "getgtype") == 0)
    key = &core_addr_getgtype;
  else
    luaL_argerror (L, 1, "invalid key");

  lua_pushlightuserdata (L, key);
  lua_pushvalue (L, 2);
  lua_rawset (L, LUA_REGISTRYINDEX);
  return 0;
}

int
lgi_type_get_name (lua_State *L, GIBaseInfo *info)
{
  GSList *list = NULL, *i;
  int n = 1;
  lua_pushstring (L, g_base_info_get_namespace (info));

  /* Add names on the whole path, but in reverse order. */
  for (; info != NULL; info = g_base_info_get_container (info))
    if (!GI_IS_TYPE_INFO (info))
      list = g_slist_prepend (list, info);

  for (i = list; i != NULL; i = g_slist_next (i))
    {
      if (g_base_info_get_type (i->data) != GI_INFO_TYPE_TYPE)
	{
	  lua_pushstring (L, ".");
	  lua_pushstring (L, g_base_info_get_name (i->data));
	  n += 2;
	}
    }

  g_slist_free (list);
  return n;
}

GType
lgi_type_get_repotype (lua_State *L, GType gtype, GIBaseInfo *info)
{
  luaL_checkstack (L, 4, "");

  /* Get repo table. */
  lua_pushlightuserdata (L, &lgi_addr_repo);
  lua_rawget (L, LUA_REGISTRYINDEX);

  /* Prepare gtype, if not given directly. */
  if (gtype == G_TYPE_INVALID && info && GI_IS_REGISTERED_TYPE_INFO (info))
    {
      gtype = g_registered_type_info_get_g_type (info);
      if (gtype == G_TYPE_NONE)
	gtype = G_TYPE_INVALID;
    }

  /* First of all, check direct indexing of repo by gtype, is fastest. */
  if (gtype != G_TYPE_INVALID)
    {
      lua_pushnumber (L, gtype);
      lua_rawget (L, -2);
    }
  else
    lua_pushnil (L);

  if (lua_isnil (L, -1))
    {
      /* Not indexed yet.  Try to lookup by name - this works when
	 lazy-loaded repo tables are not loaded yet. */
      if (!info)
	{
	  info = g_irepository_find_by_gtype (NULL, gtype);
	  lgi_gi_info_new (L, info);
	}
      else
	/* Keep stack balanced as in the previous 'if' branch. */
	lua_pushnil (L);

      if (info)
	{
	  lua_getfield (L, -3, g_base_info_get_namespace (info));
	  lua_getfield (L, -1, g_base_info_get_name (info));
	  lua_replace (L, -4);
	  lua_pop (L, 2);
	  if (gtype == G_TYPE_INVALID && !lua_isnil (L, -1))
	    {
	      lua_getfield (L, -1, "_gtype");
	      gtype = luaL_optnumber (L, -1, G_TYPE_INVALID);
	      lua_pop (L, 1);
	    }
	}
      else
	lua_pop (L, 1);
    }
  lua_replace (L, -2);
  return gtype;
}

GType
lgi_type_get_gtype (lua_State *L, int narg)
{
  /* Handle simple cases natively, forward to Lua implementation for
     the rest. */
  switch (lua_type (L, narg))
    {
    case LUA_TNIL:
    case LUA_TNONE:
      return G_TYPE_INVALID;

    case LUA_TNUMBER:
      return lua_tonumber (L, narg);

    case LUA_TSTRING:
      return g_type_from_name (lua_tostring (L, narg));

    default:
      {
	GType gtype = G_TYPE_INVALID;
	lua_pushlightuserdata (L, &core_addr_getgtype);
	lua_rawget (L, LUA_REGISTRYINDEX);
	if (!lua_isnil (L, -1))
	  {
	    lua_pushvalue (L, narg);
	    lua_call (L, 1, 1);
	    gtype = lgi_type_get_gtype (L, -1);
	  }
	lua_pop (L, 1);
	return gtype;
      }
    }
}

typedef struct _Guard
{
  gpointer data;
  GDestroyNotify destroy;
} Guard;
#define UD_GUARD "lgi.guard"

static int
guard_gc (lua_State *L)
{
  Guard *guard = lua_touserdata (L, 1);
  if (guard->data != NULL)
    guard->destroy (guard->data);
  return 0;
}

gpointer *
lgi_guard_create (lua_State *L, GDestroyNotify destroy)
{
  Guard *guard = lua_newuserdata (L, sizeof (Guard));
  g_assert (destroy != NULL);
  luaL_getmetatable (L, UD_GUARD);
  lua_setmetatable (L, -2);
  guard->data = NULL;
  guard->destroy = destroy;
  return &guard->data;
}

/* Converts GType from/to string. */
static int
core_gtype (lua_State *L)
{
  GType gtype = lgi_type_get_gtype (L, 1);
  if (lua_isnumber (L, 1))
    lua_pushstring (L, g_type_name (gtype));
  else
    lua_pushnumber (L, gtype);
  return 1;
}

/* Instantiate constant from given gi_info. */
static int
core_constant (lua_State *L)
{
  /* Get typeinfo of the constant. */
  GIArgument val;
  GIConstantInfo *ci = *(GIConstantInfo **) luaL_checkudata (L, 1, LGI_GI_INFO);
  GITypeInfo *ti = g_constant_info_get_type (ci);
  lgi_gi_info_new (L, ti);
  g_constant_info_get_value (ci, &val);
  lgi_marshal_arg_2lua (L, ti, GI_TRANSFER_NOTHING, &val, 0, FALSE,
			NULL, NULL);
  return 1;
}

/* Value contents 'access' function. Lua-side prototype:
   res = core.value(val)
   core.value(val, newval) */
static int
core_value (lua_State *L)
{
  GValue *val;
  lgi_type_get_repotype (L, G_TYPE_VALUE, NULL);
  val = lgi_record_2c (L, 1, FALSE, FALSE);
  if (lua_isnone (L, 2))
    {
      /* Read the value from GValue. */
      lgi_marshal_val_2lua (L, NULL, GI_TRANSFER_NOTHING, val);
      return 1;
    }
  else
    {
      /* Write value to GValue, or reset it if source is 'nil'. */
      if (lua_isnil (L, 2))
	g_value_reset (val);
      else
	lgi_marshal_val_2c (L, NULL, GI_TRANSFER_NOTHING, val, 2);
      return 0;
    }
}

static const char* log_levels[] = {
  "ERROR", "CRITICAL", "WARNING", "MESSAGE", "INFO", "DEBUG", "???", NULL
};

static int
core_log (lua_State *L)
{
  const char *domain = luaL_checkstring (L, 1);
  int level = 1 << (luaL_checkoption (L, 2, log_levels[5], log_levels) + 2);
  const char *message = luaL_checkstring (L, 3);
  g_log (domain, level, "%s", message);
  return 0;
}

static void
log_handler (const gchar *log_domain, GLogLevelFlags log_level,
	     const gchar *message, gpointer user_data)
{
  lua_State *L = user_data;
  const gchar **level;
  gboolean handled = FALSE, throw = FALSE;
  gint level_bit;

  /* Convert log_level to string. */
  level = log_levels;
  for (level_bit = G_LOG_LEVEL_ERROR; level_bit <= G_LOG_LEVEL_DEBUG;
       level_bit <<= 1, level++)
    if (log_level & level_bit)
      break;

  /* Check, whether there is handler registered in Lua. */
  luaL_checkstack (L, 4, "");
  lua_pushlightuserdata (L, &core_addr_logger);
  lua_rawget (L, LUA_REGISTRYINDEX);
  if (!lua_isnil (L, -1))
    {
      /* Push arguments and invoke custom log handler. */
      lua_pushstring (L, log_domain);
      lua_pushstring (L, *level);
      lua_pushstring (L, message);
      switch (lua_pcall (L, 3, 1, 0))
	{
	case 0:
	  /* If function returns non-nil, do not report on our own. */
	  handled = lua_toboolean (L, -1);
	  break;

	case LUA_ERRRUN:
	  /* Force throwing an exception. */
	  throw = TRUE;
	  break;

	default:
	  break;
	}
    }

  /* Stack cleanup; either nil, boolean or err is popped. */
  lua_pop (L, 1);

  /* In case that the level was fatal, throw a lua error. */
  if (throw || log_level & (G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR))
    luaL_error (L, "%s-%s **: %s", log_domain, *level, message);

  /* If not handled by our handler, use default system logger. */
  if (!handled)
    g_log_default_handler (log_domain, log_level, message, NULL);
}

static const struct luaL_reg lgi_reg[] = {
  { "set", core_set },
  { "log",  core_log },
  { "gtype", core_gtype },
  { "constant", core_constant },
  { "value", core_value },
  { NULL, NULL }
};

int lgi_addr_repo;

int
luaopen_lgi__core (lua_State* L)
{
  /* Early GLib initializations. Make sure that following G_TYPEs are
     already initialized, because GIRepo does not initialize them (it
     does not know that they are boxed). */
  g_type_init ();
  volatile GType unused;
  unused = G_TYPE_DATE;
  unused = G_TYPE_REGEX;
  unused = G_TYPE_DATE_TIME;
  unused = G_TYPE_VARIANT_TYPE;

  /* Register 'guard' metatable. */
  luaL_newmetatable (L, UD_GUARD);
  lua_pushcfunction (L, guard_gc);
  lua_setfield (L, -2, "__gc");
  lua_pop (L, 1);

  /* Register _core interface. */
  luaL_register (L, "lgi._core", lgi_reg);

  /* Create repo table. */
  lua_newtable (L);
  lua_pushlightuserdata (L, &lgi_addr_repo);
  lua_pushvalue (L, -2);
  lua_rawset (L, LUA_REGISTRYINDEX);
  lua_setfield (L, -2, "repo");

  /* Install custom log handler. */
  g_log_set_default_handler (log_handler, L);

  /* Initialize modules. */
  lgi_buffer_init (L);
  lgi_gi_init (L);
  lgi_record_init (L);
  lgi_object_init (L);
  lgi_callable_init (L);

  /* Return registration table. */
  return 1;
}
