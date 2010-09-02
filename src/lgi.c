/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#include "lgi.h"

int lgi_regkey;
GIBaseInfo* lgi_baseinfo_info;

int
lgi_error(lua_State* L, GError* err)
{
  lua_pushboolean(L, 0);
  if (err != NULL)
    {
      lua_pushstring(L, err->message);
      lua_pushinteger(L, err->code);
      g_error_free(err);
      return 3;
    }
  else
    return 1;
}

static int
lgi_throw(lua_State* L, GError* err)
{
  g_assert(err != NULL);
  lua_pushfstring(L, "%s (%d)", err->message, err->code);
  g_error_free(err);
  return luaL_error(L, "%s", lua_tostring(L, -1));
}

/* Allocates/initializes specified object (if applicable), stores it
   on the stack. */
static int
lgi_type_new(lua_State* L, GIBaseInfo* ii, GIArgument* val)
{
  int vals = 0;
  switch (g_base_info_get_type(ii))
    {
    case GI_INFO_TYPE_FUNCTION:
      vals = lgi_callable_create(L, ii);
      break;

    case GI_INFO_TYPE_STRUCT:
    case GI_INFO_TYPE_OBJECT:
      vals = lgi_compound_create_struct(L, ii, &val->v_pointer);
      break;

    case GI_INFO_TYPE_CONSTANT:
      {
	GITypeInfo* ti = g_constant_info_get_type(ii);
	GIArgument val;
	g_constant_info_get_value(ii, &val);
        vals = lgi_marshal_2lua(L, ti, &val, GI_TRANSFER_NOTHING, NULL, NULL)
          ? 1 : 0;
	g_base_info_unref(ti);
      }
      break;

    default:
      break;
    }

  return vals;
}

/* Puts parts of the name to the stack, to be concatenated by lua_concat.
   Returns number of pushed elements. */
int
lgi_type_get_name(lua_State* L, GIBaseInfo* info)
{
  GSList* list = NULL, *i;
  int n = 1;
  lua_pushstring(L, g_base_info_get_namespace(info));

  /* Add names on the whole path, but in reverse order. */
  for (; info != NULL; info = g_base_info_get_container(info))
    list = g_slist_prepend(list, info);

  for (i = list; i != NULL; i = g_slist_next(i))
    {
      lua_pushstring(L, ".");
      lua_pushstring(L, g_base_info_get_name(i->data));
      n += 2;
    }

  g_slist_free(list);
  return n;
}

static int
lgi_find(lua_State* L)
{
  const gchar* symbol = luaL_checkstring(L, 1);
  const gchar* container = luaL_optstring(L, 2, NULL);
  GIBaseInfo *info, *fi;
  int vals = 0;

  /* Get information about the symbol. */
  info = g_irepository_find_by_name(NULL, "GIRepository",
				    container != NULL ? container : symbol);

  /* In case that container was specified, look the symbol up in it. */
  if (container != NULL && info != NULL)
    {
      switch (g_base_info_get_type(info))
	{
	case GI_INFO_TYPE_OBJECT:
	  fi = g_object_info_find_method(info, symbol);
	  break;

	case GI_INFO_TYPE_INTERFACE:
	  fi = g_interface_info_find_method(info, symbol);
	  break;

	case GI_INFO_TYPE_STRUCT:
	  fi = g_struct_info_find_method(info, symbol);
	  break;

	default:
	  fi = NULL;
	}

      g_base_info_unref(info);
      info = fi;
    }

  if (info == NULL)
    {
      lua_pushboolean(L, 0);
      lua_pushfstring(L, "unable to resolve GIRepository.%s%s%s",
		      container != NULL ? container : "",
		      container != NULL ? ":" : "",
		      symbol);
      return 2;
    }

  /* Create new IBaseInfo structure and return it. */
  vals = lgi_compound_create(L, lgi_baseinfo_info, info, TRUE) ? 1 : 0;
  return vals;
}

static int
lgi_get(lua_State* L)
{
  /* Create new instance based on the embedded typeinfo. */
  GIArgument unused;
  return lgi_type_new(L, lgi_compound_get(L, 1, lgi_baseinfo_info, FALSE),
                      &unused);
}

#ifndef NDEBUG
static const char* lgi_log_levels[] =
  { "error", "critical", "warning", "message", "info", "debug", NULL };
static int
lgi_log(lua_State* L)
{
  const char* message = luaL_checkstring(L, 1);
  int level = 1 << (luaL_checkoption(L, 2, lgi_log_levels[5],
				     lgi_log_levels) + 2);
  g_log(G_LOG_DOMAIN, level, "%s", message);
  return 0;
}

const char* lgi_sd(lua_State *L)
{
  int i;
  static gchar* msg = 0;
  g_free(msg);
  msg = g_strdup("");
  int top = lua_gettop(L);
  for (i = 1; i <= top; i++) {	/* repeat for each level */
    int t = lua_type(L, i);
    gchar* item, *nmsg;
    switch (t) {
    case LUA_TSTRING:  /* strings */
      item = g_strdup_printf("`%s'", lua_tostring(L, i));
      break;

    case LUA_TBOOLEAN:	/* booleans */
      item = g_strdup_printf(lua_toboolean(L, i) ? "true" : "false");
      break;

    case LUA_TNUMBER:  /* numbers */
      item = g_strdup_printf("%g", lua_tonumber(L, i));
      break;

    default:  /* other values */
      item = g_strdup_printf("%s(%p)", lua_typename(L, t), lua_topointer(L, i));
      break;
    }
    nmsg = g_strconcat(msg, " ", item, NULL);
    g_free(msg);
    g_free(item);
    msg = nmsg;
  }
  return msg;
}
#endif

static const struct luaL_reg lgi_reg[] = {
  { "find", lgi_find },
  { "get", lgi_get },
#ifndef NDEBUG
  { "log", lgi_log },
#endif
  { NULL, NULL }
};

static void
lgi_reg_udata(lua_State* L, const struct luaL_reg* reg, const char* meta)
{
  luaL_newmetatable(L, meta);
  luaL_register(L, NULL, reg);
  lua_pop(L, 1);
}

static void
lgi_create_reg(lua_State* L, enum lgi_reg reg, const char* exportname,
	       gboolean withmeta)
{
  /* Create the table. */
  lua_newtable(L);

  /* Assign the metatable, if requested. */
  if (withmeta)
    {
      lua_pushvalue(L, -2);
      lua_setmetatable(L, -2);
      lua_replace(L, -2);
    }

  /* Assign table into the exported package table. */
  if (exportname != NULL)
    {
      lua_pushstring(L, exportname);
      lua_pushvalue(L, -2);
      lua_rawset(L, -5);
    }

  /* Assign new table into registry and leave it out from stack. */
  lua_rawseti(L, -2, reg);
}

lua_State* lgi_main_thread_state;

int
luaopen_lgi__core(lua_State* L)
{
  GError* err = NULL;

  /* Remember state of the main thread. */
  lgi_main_thread_state = L;

  /* GLib initializations. */
  g_type_init();
  g_irepository_require(NULL, "GIRepository", NULL, 0, &err);
  if (err != NULL)
    lgi_throw(L, err);
  lgi_baseinfo_info = g_irepository_find_by_name(NULL, "GIRepository",
						 "IBaseInfo");

  /* Register userdata types. */
  lgi_reg_udata(L, lgi_compound_reg, LGI_COMPOUND);
  lgi_reg_udata(L, lgi_callable_reg, LGI_CALLABLE);
  lgi_reg_udata(L, lgi_closureguard_reg, LGI_CLOSUREGUARD);

  /* Register _core interface. */
  luaL_register(L, "lgi._core", lgi_reg);

  /* Prepare registry table (avoid polluting global registry, make
     private table in it instead.*/
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lgi_regkey = luaL_ref(L, LUA_REGISTRYINDEX);

  /* Create object cache, which has weak values. */
  lua_newtable(L);
  lua_pushstring(L, "v");
  lua_setfield(L, -2, "__mode");
  lgi_create_reg(L, LGI_REG_CACHE, NULL, TRUE);

  /* Create typeinfo table. */
  lgi_create_reg(L, LGI_REG_TYPEINFO, NULL, FALSE);

  /* Create repo table. */
  lgi_create_reg(L, LGI_REG_REPO, "repo", FALSE);

  /* In debug version, make our private registry browsable. */
#ifndef NDEBUG
  lua_pushstring(L, "reg");
  lua_pushvalue(L, -2);
  lua_rawset(L, -4);
#endif

  /* Pop the registry table, return registration table. */
  lua_pop(L, 1);
  return 1;
}
