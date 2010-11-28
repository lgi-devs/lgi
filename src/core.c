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

/* Puts parts of the name to the stack, to be concatenated by lua_concat.
   Returns number of pushed elements. */
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

int
lgi_guard_create (lua_State *L, gpointer **data, GDestroyNotify destroy)
{
  Guard *guard = lua_newuserdata (L, sizeof (Guard));
  g_assert (destroy != NULL);
  luaL_getmetatable (L, UD_GUARD);
  lua_setmetatable (L, -2);
  guard->data = NULL;
  guard->destroy = destroy;
  *data = &guard->data;
  return lua_gettop (L);
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
  GValue *val;
  GType gtype;

  /* Check, whether arg1 is GValue. */
  gtype = G_TYPE_VALUE;
  val = lgi_compound_check (L, 1, &gtype);
  if (val != NULL)
    {
      /* Construct from value just unboxes the real value from it. */
      lgi_marshal_val_2lua (L, NULL, GI_TRANSFER_NOTHING, val);
      return 1;
    }

  /* Check whether arg1 is baseinfo. */
  info = luaL_checkudata (L, 1, LGI_GI_INFO);
  switch (g_base_info_get_type (*info))
    {
    case GI_INFO_TYPE_FUNCTION:
      vals = lgi_callable_create (L, *info);
      break;

    case GI_INFO_TYPE_STRUCT:
    case GI_INFO_TYPE_UNION:
      {
	GType type = g_registered_type_info_get_g_type (*info);
	if (g_type_is_a (type, G_TYPE_CLOSURE))
	  {
	    /* Create closure instance wrapping 2nd argument and
	       return it. */
	    lgi_record_2lua (L, *info, lgi_gclosure_create (L, 2),
			     LGI_RECORD_OWN, 0);
	    vals = 1;
	  }


	else if (g_type_is_a (type, G_TYPE_VALUE))
	  {
	    /* Get requested GType, construct and fill in GValue
	       and return it wrapped in a GBoxed which is wrapped in
	       a compound. */
	    GValue val = {0};
	    type = luaL_checknumber (L, 2);
	    if (G_TYPE_IS_VALUE (type))
	      {
		g_value_init (&val, type);
		lgi_marshal_val_2c (L, NULL, GI_TRANSFER_NOTHING,
				    &val, 3);
	      }

	    lgi_record_2lua (L, *info, g_boxed_copy (G_TYPE_VALUE, &val),
			     LGI_RECORD_OWN, 0);
	    vals = 1;
	    if (G_IS_VALUE (&val))
	      g_value_unset (&val);
	  }
	else
	  {
	    /* Create common struct. */
	    lgi_record_2lua (L, *info, NULL, LGI_RECORD_ALLOCATE, 0);
	    vals = 1;
	  }
	break;
      }

    case GI_INFO_TYPE_OBJECT:
      lgi_compound_object_new (L, *info, 2);
      vals = 1;
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

static int
lgi_gtype (lua_State *L)
{
  GType gtype = G_TYPE_NONE;

  if (lua_type (L, 1) == LUA_TSTRING)
    {
      /* Get information about the name. */
      const gchar *name = luaL_checkstring (L, 1);
      GIBaseInfo *info;

      info = g_irepository_find_by_name (NULL, "GIRepository", name);
      if (info == NULL)
	return luaL_error (L, "unable to resolve GIRepository.%s", name);

      gtype = g_registered_type_info_get_g_type (info);
      g_base_info_unref (info);
    }
  else
    {
      /* Get information by compound. */
      if (!lgi_compound_check (L, 1, &gtype))
	gtype = lgi_record_gtype (L, 1);
    }

  lua_pushnumber (L, gtype);
  return 1;
}

static void
gclosure_destroy (gpointer user_data, GClosure *closure)
{
  lgi_closure_destroy (user_data);
}

/* Connects signal to given compound.
 * Signature is:
 * handler_id = core.connect(obj, signame, callable, func, detail, after) */
static int
lgi_connect (lua_State *L)
{
  gpointer obj;
  const char *signame = luaL_checkstring (L, 2);
  GICallableInfo *ci;
  const char *detail = lua_tostring (L, 5);
  gpointer call_addr, lgi_closure;
  GClosure *gclosure;
  guint signal_id;
  gulong handler_id;
  GType gt_obj = G_TYPE_OBJECT;

  /* Get target objects. */
  lgi_compound_get (L, 1, &gt_obj, &obj, 0);
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
  signal_id = g_signal_lookup (signame, G_OBJECT_TYPE (obj));
  handler_id =  g_signal_connect_closure_by_id (obj, signal_id,
						g_quark_from_string (detail),
						gclosure, lua_toboolean (L, 6));
  lua_pushnumber (L, handler_id);
  return 1;
}

static int
lgi_setlogger(lua_State *L)
{
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_pushvalue (L, 1);
  lua_rawseti (L, -2, LGI_REG_LOG_HANDLER);
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
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti (L, -1, LGI_REG_LOG_HANDLER);
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

  /* Stack cleanup; either (reg,nil) or (reg,err) are popped. */
  lua_pop (L, 2);

  /* In case that the level was fatal, throw a lua error. */
  if (throw || log_level & (G_LOG_FLAG_FATAL | G_LOG_LEVEL_ERROR))
    luaL_error (L, "%s-%s **: %s", log_domain, *level, message);

  /* If not handled by our handler, use default system logger. */
  if (!handled)
    g_log_default_handler (log_domain, log_level, message, NULL);
}

static const struct luaL_reg lgi_reg[] = {
  { "construct", lgi_construct },
  { "gtype", lgi_gtype },
  { "connect", lgi_connect },
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
  lgi_compound_init (L);
  lgi_callable_init (L);

  /* Return registration table. */
  return 1;
}
