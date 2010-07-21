/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#include <lua.h>
#include <lauxlib.h>

#include <girepository.h>
#include <girffi.h>

static int lgi_error(lua_State* L, GError* err)
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

static int lgi_throw(lua_State* L, GError* err)
{
  if (err != NULL)
    {
      lua_pushfstring(L, "%s (%d)", err->message, err->code);
      g_error_free(err);
    }
  else
    lua_pushstring(L, "unspecified GError-NULL");

  return lua_error(L);
}

/* 'function' userdata: wraps function prepared to be called through ffi. */
struct ud_function
{
  GIFunctionInvoker invoker;
  GIFunctionInfo* info;
};
#define UD_FUNCTION "lgi.function"

static void function_new(lua_State* L, GIFunctionInfo* info)
{
  struct ud_function* function;
  const gchar* name;
  gpointer addr;
  GError* err = NULL;

  /* Get address of the function. */
  name = g_function_info_get_symbol(info);
  if (!g_typelib_symbol(g_base_info_get_typelib(info), name, &addr))
    luaL_error(L, "can't load function %s.%s(%s)",
                      g_base_info_get_namespace(info),
                      g_base_info_get_name(info), name);

  /* Create new userdata value. */
  function = lua_newuserdata(L, sizeof(struct ud_function));
  function->info = info;
  if (!g_function_info_prep_invoker(info, &function->invoker, &err))
    lgi_throw(L, err);
}

static int function_gc(lua_State* L)
{
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  g_function_invoker_destroy(&function->invoker);
  g_object_unref(function->info);
  return 0;
}

union typebox
{
  gboolean gboolean_;
  gint8 gint8_;
  guint8 guint8_;
  gint16 gint16_;
  guint16 guint16_;
  gint32 gint32_;
  guint32 guint32_;
  gint64 gint64_;
  guint64 guint64_;
  gshort gshort_;
  gushort gushort_;
  gint gint_;
  guint guint_;
  glong glong_;
  gulong gulong_;
  gssize gssize_;
  gsize gsize_;
  gfloat gfloat_;
  gdouble gdouble_;
  time_t time_t_;
  GType GType_;
};

static void function_arg_in(lua_State* L, int argi, GITypeInfo* info,
                            const void** arg_ptr, union typebox* arg_val)
{
  switch (g_type_info_get_tag(info))
    {
#define TYPE_CASE(tag, type, expr)           \
  case GI_TYPE_TAG_ ## tag :                 \
    arg_val->type ## _ = (type) expr;        \
    break

      TYPE_CASE(BOOLEAN, gboolean, lua_toboolean(L, argi));
      TYPE_CASE(INT8, gint8, luaL_checkinteger(L, argi));
      TYPE_CASE(UINT8, guint8, luaL_checkinteger(L, argi));
      TYPE_CASE(INT16, gint16, luaL_checkinteger(L, argi));
      TYPE_CASE(UINT16, guint16, luaL_checkinteger(L, argi));
      TYPE_CASE(INT32, guint32, luaL_checkinteger(L, argi));
      TYPE_CASE(INT64, gint64, luaL_checkinteger(L, argi));
      TYPE_CASE(UINT64, guint64, luaL_checkinteger(L, argi));
      TYPE_CASE(SHORT, gshort, luaL_checkinteger(L, argi));
      TYPE_CASE(USHORT, gushort, luaL_checkinteger(L, argi));
      TYPE_CASE(INT, gint, luaL_checkinteger(L, argi));
      TYPE_CASE(UINT, guint, luaL_checkinteger(L, argi));
      TYPE_CASE(LONG, glong, luaL_checkinteger(L, argi));
      TYPE_CASE(ULONG, gulong, luaL_checkinteger(L, argi));
      TYPE_CASE(SSIZE, gssize, luaL_checkinteger(L, argi));
      TYPE_CASE(SIZE, gsize, luaL_checkinteger(L, argi));
      TYPE_CASE(FLOAT, gfloat, luaL_checkinteger(L, argi));
      TYPE_CASE(DOUBLE, gdouble, luaL_checkinteger(L, argi));
      TYPE_CASE(GTYPE, GType, luaL_checkinteger(L, argi));

#undef TYPE_CASE

    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      *arg_ptr = luaL_checkstring(L, argi);
      break;

    default:
      /* TODO: Handle the complex ones. */
      break;
    }
}

static int function_call(lua_State* L)
{
  gint argc, flags, has_self, throws, argi, lua_argi, ti_argi;
  const void** args_ptr;
  union typebox* args_val;
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  GError* err = NULL;
  GITypeInfo* ti;

  /* If function is a method, it has implicit 'self' parameter. */
  flags = g_function_info_get_flags(function->info);
  has_self = (flags & GI_FUNCTION_IS_METHOD) != 0 &&
    (flags & GI_FUNCTION_IS_CONSTRUCTOR) == 0;
  throws = (flags & GI_FUNCTION_THROWS) != 0;
  argc = g_callable_info_get_n_args(function->info) + has_self + throws;

  /* Allocate array for arguments. */
  args_ptr = g_newa(const void*, argc);
  args_val = g_newa(union typebox, argc);
  lua_argi = 1;
  ti_argi = 0;
  for (argi = 0; argi < argc; argi++)
    {
      if (argi == 0 && has_self)
        {
          /* Handle 'self' parameter. */
          lua_argi++;
        }
      else if (argi == argc - 1 && throws)
        {
          /* Handle 'err' parameter. */
          args_ptr[argi] = &err;
        }
      else
        {
          /* Handle ordinary parameter. */
          GIArgInfo* ai = g_callable_info_get_arg(function->info, ti_argi++);
          GIDirection dir = g_arg_info_get_direction(ai);
          ti = g_arg_info_get_type(ai);
          args_ptr[argi] = &args_val[argi];
          if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT)
            function_arg_in(L, lua_argi++, ti,
                            &args_ptr[argi], &args_val[argi]);
          g_base_info_unref(ai); 
        }
    }

  return 0;
}

static const struct luaL_reg function_reg[] = {
  { "__gc", function_gc },
  { "__call", function_call },
  { NULL, NULL }
};

static int prepare_function(lua_State* L, GIFunctionInfo* info)
{
  lua_pushstring(L, "function");
  lua_pushstring(L, "info");
  return 2;
}

/*
   lgi._prepare(namespace, symbolname)

   Prepares symbol from given namespace.  Returns type of the symbol and
   additional symbol data, depending on symbol type.
*/
static int lgi_prepare(lua_State* L)
{
  GError* err = NULL;
  const gchar* namespace_ = luaL_checkstring(L, 1);
  const gchar* symbol = luaL_checkstring(L, 2);
  GIBaseInfo* info;
  GIInfoType type;

  /* Make sure that the repository is loaded. */
  if (g_irepository_require(NULL, namespace_, NULL, 0, &err) == NULL)
    return lgi_error(L, err);

  /* Get information about the symbol. */
  info = g_irepository_find_by_name(NULL, namespace_, symbol);

  /* Check the type of the symbol. */
  if (info != NULL)
    {
      type = g_base_info_get_type(info);
      switch (type)
        {
        case GI_INFO_TYPE_FUNCTION:
          return prepare_function(L, info);

        default:
          break;
        }
    }

  /* If the symbol was not handled inside the switch above, it does not
     exist. */
  lua_pushnil(L);
  return 1;
}

/*
  lgi._call(funcdata, ...)

  Calls function, previously prepared by lgi._prepare call.  funcdata is
  additional data returned by lgi._prepare.
*/
static int lgi_call(lua_State* L)
{
  return 0;
}

static const struct luaL_reg lgi_reg[] = {
  { "_prepare", lgi_prepare },
  { "_call", lgi_call },
  { NULL, NULL }
};

int luaopen_lgi(lua_State* L)
{
  g_type_init();
  luaL_newmetatable(L, UD_FUNCTION);
  luaL_register(L, NULL, function_reg);
  lua_pop(L, 1);
  luaL_register(L, "lgi", lgi_reg);
  return 1;
}
