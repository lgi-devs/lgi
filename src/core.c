/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
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

int
lgi_guard_create_baseinfo (lua_State *L, GIBaseInfo *info)
{
  gpointer *data;
  int res = lgi_guard_create (L, &data, (GDestroyNotify) g_base_info_unref);
  *data = info;
  return res;
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
  int vals, info_guard;

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
  info_guard = lgi_guard_create_baseinfo (L, baseinfo);
  vals = lgi_compound_create (L, baseinfo, info, TRUE, 0);
  lua_remove (L, info_guard);
  return vals;
}

static int
lgi_construct (lua_State* L)
{
  /* Create new instance based on the embedded typeinfo. */
  int vals = 0;
  GIBaseInfo *bi;
  GType gtype;
  GValue *val;

  /* Check, whether arg1 is GValue. */
  gtype = G_TYPE_VALUE;
  val = lgi_compound_check (L, 1, &gtype);
  if (val != NULL)
    /* Construct from value just unboxes the real value from it. */
    return lgi_value_store (L, val);

  /* Check whether arg1 is baseinfo. */
  gtype = GI_TYPE_BASE_INFO;
  bi = lgi_compound_check (L, 1, &gtype);
  if (bi != NULL)
    {
      switch (g_base_info_get_type (bi))
	{
	case GI_INFO_TYPE_FUNCTION:
	  vals = lgi_callable_create (L, bi);
	  break;

	case GI_INFO_TYPE_STRUCT:
	case GI_INFO_TYPE_UNION:
	  {
	    GType type = g_registered_type_info_get_g_type (bi);
	    if (g_type_is_a (type, G_TYPE_CLOSURE))
	      /* Create closure instance wrapping 2nd argument and return it. */
	      vals = lgi_compound_create (L, bi, lgi_gclosure_create (L, 2),
					  TRUE, 0);
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
                    lgi_value_load (L, &val, 3);
                  }

                vals = lgi_compound_create (L, bi,
					    g_boxed_copy (G_TYPE_VALUE, &val),
					    TRUE, 0);
                if (G_IS_VALUE (&val))
                  g_value_unset (&val);
	      }
	    else
	      {
		/* Create common struct. */
		lgi_compound_struct_new (L, bi);
		vals = 1;
	      }
	    break;
	  }

	case GI_INFO_TYPE_OBJECT:
	  lgi_compound_object_new (L, bi, 2);
	  vals = 1;
	  break;

	case GI_INFO_TYPE_CONSTANT:
	  {
	    GITypeInfo *ti = g_constant_info_get_type (bi);
	    GIArgument val;
            int ti_guard = lgi_guard_create_baseinfo (L, ti);
	    g_constant_info_get_value (bi, &val);
	    lgi_marshal_2lua (L, ti, GI_TRANSFER_NOTHING, &val, 0, FALSE,
			      NULL, NULL);
	    vals = 1;
            lua_remove (L, ti_guard);
	  }
	  break;

	default:
	  lua_pushfstring (L, "failing to construct unknown type %d (%s.%s)",
			   g_base_info_get_type (bi),
			   g_base_info_get_namespace (bi),
			   g_base_info_get_name (bi));
	  g_warning ("%s", lua_tostring (L, -1));
	  lua_error (L);
	  break;
	}

      return vals;
    }

  return luaL_typerror (L, 1, "(lgi userdata)");
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
      gpointer unused;
      lua_pop (L, lgi_compound_get (L, 1, &gtype, &unused, FALSE));
    }

  lua_pushnumber (L, gtype);
  return 1;
}

/* Checks, whether given compound is object which can be cast to requested
   gtype, and if yes, creates new compound of requested type. */
static int
lgi_cast (lua_State *L)
{
  GObject *obj;
  GType gtype = luaL_checknumber (L, 2), gt_obj = G_TYPE_OBJECT;

  /* Get the source object. */
  lgi_compound_get (L, 1, &gt_obj, (gpointer *) &obj, FALSE);

  /* Check, that casting is possible. */
  if (g_type_is_a (G_TYPE_FROM_INSTANCE (obj), gtype))
    {
      GIBaseInfo *info = g_irepository_find_by_gtype (NULL, gtype);
      if (info != NULL)
	{
          int info_guard = lgi_guard_create_baseinfo (L, info);
	  lgi_compound_create (L, info, g_object_ref (obj), TRUE, 0);
	  lua_remove (L, info_guard);
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
  gpointer obj;
  const char *signame = luaL_checkstring (L, 2);
  GICallableInfo *ci;
  const char *detail = lua_tostring (L, 5);
  gpointer call_addr, lgi_closure;
  GClosure *gclosure;
  guint signal_id;
  gulong handler_id;
  GType gt_obj = G_TYPE_OBJECT, gt_bi = GI_TYPE_BASE_INFO;

  /* Get target objects. */
  if (lgi_compound_get (L, 1, &gt_obj, &obj, FALSE)
      || lgi_compound_get (L, 3, &gt_bi, (gpointer *) &ci, FALSE))
      g_assert_not_reached ();

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

static const struct luaL_reg lgi_reg[] = {
  { "find", lgi_find },
  { "construct", lgi_construct },
  { "gtype", lgi_gtype },
  { "cast", lgi_cast },
  { "connect", lgi_connect },
  { "log", lgi_glib_log },
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
  GError* err = NULL;

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

  /* Initialize modules. */
  lgi_glib_init (L);
  lgi_compound_init (L);
  lgi_callable_init (L);

  /* Pop the registry table, return registration table. */
  lua_pop (L, 1);
  return 1;
}
