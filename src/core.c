/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Core C utility API.
 */

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

void
lgi_guard_get_data (lua_State *L, int pos, gpointer **data)
{
  Guard *guard = lua_touserdata (L, pos);
  *data = &guard->data;
}

static int
lgi_construct (lua_State* L)
{
  /* Create new instance based on the embedded typeinfo. */
  int vals = 0;
  GIBaseInfo **info;

  /* Check whether arg1 is baseinfo. */
  info = luaL_checkudata (L, 1, LGI_GI_INFO);
  switch (g_base_info_get_type (*info))
    {
    case GI_INFO_TYPE_FUNCTION:
      vals = lgi_callable_create (L, *info);
      break;

    case GI_INFO_TYPE_CONSTANT:
      {
	GITypeInfo *ti = g_constant_info_get_type (*info);
	GIArgument val;
	lgi_gi_info_new (L, ti);
	g_constant_info_get_value (*info, &val);
	lgi_marshal_arg_2lua (L, ti, GI_TRANSFER_NOTHING, &val, 0, FALSE,
			      NULL, NULL);
	vals = 1;
      }
      break;

    default:
      lua_pushfstring (L, "failing to construct unknown type %d (%s.%s)",
		       g_base_info_get_type (*info),
		       g_base_info_get_namespace (*info),
		       g_base_info_get_name (*info));
      g_warning ("%s", lua_tostring (L, -1));
      lua_error (L);
      break;
    }

  return vals;
}

static int core_addr_logger;

static int
lgi_setlogger(lua_State *L)
{
  lua_pushlightuserdata (L, &core_addr_logger);
  lua_pushvalue (L, 1);
  lua_rawset (L, LUA_REGISTRYINDEX);
  return 0;
}

static const char* log_levels[] = {
  "ERROR", "CRITICAL", "WARNING", "MESSAGE", "INFO", "DEBUG", "???", NULL
};

static int
lgi_log (lua_State *L)
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
  { "construct", lgi_construct },
  { "log",  lgi_log },
  { "setlogger", lgi_setlogger },
  { NULL, NULL }
};

static void
lgi_create_reg (lua_State* L, enum lgi_reg reg, const char* exportname,
		gboolean withmeta)
{
  /* Create the table. */
  lua_newtable (L);

  /* Assign the metatable, if requested. */
  if (withmeta)
    {
      lua_pushvalue (L, -2);
      lua_setmetatable (L, -2);
      lua_replace (L, -2);
    }

  /* Assign table into the exported package table. */
  if (exportname != NULL)
    {
      lua_pushstring (L, exportname);
      lua_pushvalue (L, -2);
      lua_rawset (L, -5);
    }

  /* Assign new table into registry and leave it out from stack. */
  lua_rawseti (L, -2, reg);
}

int lgi_regkey;
int lgi_addr_repo;

int
luaopen_lgi__core (lua_State* L)
{
  GError *err = NULL;

  /* Early GLib initializations. */
  g_type_init ();
  g_irepository_require (NULL, "GIRepository", NULL, 0, &err);
  if (err != NULL)
    {
      lua_pushfstring (L, "%s (%d)", err->message, err->code);
      g_error_free (err);
      return luaL_error (L, "%s", lua_tostring (L, -1));
    }

  /* Register 'guard' metatable. */
  luaL_newmetatable (L, UD_GUARD);
  lua_pushcfunction (L, guard_gc);
  lua_setfield (L, -2, "__gc");
  lua_pop (L, 1);

  /* Register _core interface. */
  luaL_register (L, "lgi._core", lgi_reg);

  /* Prepare registry table (avoid polluting global registry, make
     private table in it instead.*/
  lua_newtable (L);
  lua_pushvalue (L, -1);
  lgi_regkey = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Create object cache, which has weak values. */
  lua_newtable (L);
  lua_pushstring (L, "v");
  lua_setfield (L, -2, "__mode");
  lgi_create_reg (L, LGI_REG_CACHE, NULL, TRUE);

  /* Create typeinfo table. */
  lgi_create_reg (L, LGI_REG_TYPEINFO, NULL, FALSE);

  /* Create repo table. */
  lgi_create_reg (L, LGI_REG_REPO, "repo", FALSE);
  lua_pushlightuserdata (L, &lgi_addr_repo);
  lua_rawgeti (L, -2, LGI_REG_REPO);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* In debug version, make our private registry browsable. */
#ifndef NDEBUG
  lua_pushstring (L, "reg");
  lua_pushvalue (L, -2);
  lua_rawset (L, -4);
#endif

  /* Pop the registry table. */
  lua_pop (L, 1);

  /* Install custom log handler. */
  g_log_set_default_handler (log_handler, L);

  /* Initialize modules. */
  lgi_gi_init (L);
  lgi_record_init (L);
  lgi_object_init (L);
  lgi_callable_init (L);

  /* Return registration table. */
  return 1;
}
