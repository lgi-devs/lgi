/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#include "lgi.h"

int lgi_regkey;

int
lgi_error (lua_State *L, GError *err)
{
  lua_pushboolean (L, 0);
  if (err != NULL)
    {
      lua_pushstring (L, err->message);
      lua_pushinteger (L, err->code);
      g_error_free (err);
      return 3;
    }
  else
    return 1;
}

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

lua_State *
lgi_get_callback_state (lua_State **state, int *thread_ref)
{
  /* Get access to proper Lua context. */
  lua_State *L = *state;
  lua_rawgeti (L, LUA_REGISTRYINDEX, *thread_ref);
  if (lua_isthread (L, -1))
    {
      L = lua_tothread (L, -1);
      if (lua_status (L) != 0)
	{
	  /* Thread is not in usable state for us, it is suspended, we
	     cannot afford to resume it, because it is possible that
	     the routine we are about to call is actually going to
	     resume it.  Create new thread instead and switch closure
	     to its context. */
	  L = lua_newthread (L);
	  luaL_unref (L, LUA_REGISTRYINDEX, *thread_ref);
	  *thread_ref = luaL_ref (*state, LUA_REGISTRYINDEX);
	}
    }
  lua_pop (*state, 1);
  *state = L;
  return L;
}

static int
lgi_find (lua_State *L)
{
  const gchar *symbol = luaL_checkstring (L, 1);
  const gchar *container = luaL_optstring (L, 2, NULL);
  GIBaseInfo *info, *fi, *baseinfo;
  int vals;

  /* Get information about the symbol. */
  info = g_irepository_find_by_name (NULL, "GIRepository",
				     container != NULL ? container : symbol);

  /* In case that container was specified, look the symbol up in it. */
  if (container != NULL && info != NULL)
    {
      switch (g_base_info_get_type (info))
	{
	case GI_INFO_TYPE_OBJECT:
	  fi = g_object_info_find_method (info, symbol);
	  break;

	case GI_INFO_TYPE_INTERFACE:
	  fi = g_interface_info_find_method (info, symbol);
	  break;

	case GI_INFO_TYPE_STRUCT:
	  fi = g_struct_info_find_method (info, symbol);
	  break;

	default:
	  fi = NULL;
	}

      g_base_info_unref (info);
      info = fi;
    }

  if (info == NULL)
    return luaL_error (L, "unable to resolve GIRepository.%s%s%s",
		       container != NULL ? container : "",
		       container != NULL ? ":" : "",
		       symbol);

  /* Create new IBaseInfo structure and return it. */
  baseinfo = g_irepository_find_by_name (NULL, "GIRepository", "BaseInfo");
  vals = lgi_compound_create (L, baseinfo, info, TRUE);
  g_base_info_unref (baseinfo);
  return vals;
}

static int
lgi_get (lua_State* L)
{
  /* Create new instance based on the embedded typeinfo. */
  gpointer res;
  int vals = 0;
  GIBaseInfo* ii = lgi_compound_get (L, 1, GI_TYPE_BASE_INFO, FALSE);

  switch (g_base_info_get_type (ii))
    {
    case GI_INFO_TYPE_FUNCTION:
      vals = lgi_callable_create (L, ii);
      break;

    case GI_INFO_TYPE_STRUCT:
      res = lgi_compound_struct_new (L, ii);
      vals = 1;
      break;

    case GI_INFO_TYPE_OBJECT:
      res = lgi_compound_object_new (L, ii, 2);
      vals = 1;
      break;

    case GI_INFO_TYPE_CONSTANT:
      {
	GITypeInfo* ti = g_constant_info_get_type (ii);
	GIArgument val;
	g_constant_info_get_value (ii, &val);
	vals = lgi_marshal_2lua (L, ti, &val, GI_TRANSFER_NOTHING, NULL, NULL)
	  ? 1 : 0;
	g_base_info_unref (ti);
      }
      break;

    default:
      lua_pushfstring (L, "failing to create unknown type %d (%s.%s)",
		      g_base_info_get_type (ii),
		      g_base_info_get_namespace (ii),
		      g_base_info_get_name (ii));
      g_warning ("%s", lua_tostring (L, -1));
      lua_error (L);
      break;
    }

  return vals;
}

static int
lgi_gtype (lua_State *L)
{
  const gchar *name = luaL_checkstring (L, 1);
  GIBaseInfo *info;

  /* Get information about the name. */
  info = g_irepository_find_by_name (NULL, "GIRepository", name);
  if (info == NULL)
    return luaL_error (L, "unable to resolve GIRepository.%s", name);

  lua_pushnumber (L, g_registered_type_info_get_g_type (info));
  g_base_info_unref (info);
  return 1;
}

/* Checks, whether given compound is object which can be cast to requested
   gtype, and if yes, creates new compound of requested type. */
static int
lgi_cast (lua_State *L)
{
  /* Get the source object. */
  GObject *obj = lgi_compound_get (L, 1, G_TYPE_OBJECT, FALSE);
  GType gtype = luaL_checknumber (L, 2);

  /* Check, that casting is possible. */
  if (g_type_is_a (G_TYPE_FROM_INSTANCE (obj), gtype))
    {
      GIBaseInfo *info = g_irepository_find_by_gtype (NULL, gtype);
      if (info != NULL)
	{
	  lgi_compound_create (L, info, g_object_ref (obj), TRUE);
	  g_base_info_unref (info);
	  return 1;
	}
    }

  /* Failed somehow, avoid casting. */
  return luaL_error (L, "`%s': failed to cast to `%s'",
		     g_type_name (G_TYPE_FROM_INSTANCE (obj)),
		     g_type_name (gtype));
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
  gpointer obj = lgi_compound_get (L, 1, G_TYPE_OBJECT, FALSE);
  const char *signame = luaL_checkstring (L, 2);
  GICallableInfo *ci = lgi_compound_get (L, 3, GI_TYPE_BASE_INFO, FALSE);
  const char *detail = lua_tostring (L, 5);
  gpointer call_addr, lgi_closure;
  GClosure *gclosure;
  guint signal_id;
  gulong handler_id;

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

#ifndef NDEBUG
static const char* lgi_log_levels[] =
  { "error", "critical", "warning", "message", "info", "debug", NULL };
static int
lgi_log (lua_State *L)
{
  const char *message = luaL_checkstring (L, 1);
  int level = 1 << (luaL_checkoption (L, 2, lgi_log_levels[5],
				      lgi_log_levels) + 2);
  g_log (G_LOG_DOMAIN, level, "%s", message);
  return 0;
}

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

static const struct luaL_reg lgi_reg[] = {
  { "find", lgi_find },
  { "get", lgi_get },
  { "gtype", lgi_gtype },
  { "cast", lgi_cast },
  { "connect", lgi_connect },
#ifndef NDEBUG
  { "log", lgi_log },
#endif
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

int
luaopen_lgi__core (lua_State* L)
{
  GError* err = NULL;

  /* GLib initializations. */
  g_type_init ();
  g_irepository_require (NULL, "GIRepository", NULL, 0, &err);
  if (err != NULL)
    {
      lua_pushfstring (L, "%s (%d)", err->message, err->code);
      g_error_free (err);
      return luaL_error (L, "%s", lua_tostring (L, -1));
    }

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

  /* Initialize modules. */
  lgi_compound_init (L);
  lgi_callable_init (L);
  lgi_marshal_init (L);

  /* Pop the registry table, return registration table. */
  lua_pop (L, 1);
  return 1;
}
