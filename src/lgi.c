/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

#define G_LOG_DOMAIN "Lgi"

#include <lua.h>
#include <lauxlib.h>

#include <girepository.h>
#include <girffi.h>

static int
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

static void
function_new(lua_State* L, GIFunctionInfo* info)
{
  GError* err = NULL;
  struct ud_function* function = lua_newuserdata(L, sizeof(struct ud_function));
  luaL_getmetatable(L, UD_FUNCTION);
  lua_setmetatable(L, -2);
  function->info = info;
  if (!g_function_info_prep_invoker(info, &function->invoker, &err))
    lgi_throw(L, err);
}

static int
function_gc(lua_State* L)
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
  gpointer gpointer_;
};

static void
function_arg_in(lua_State* L, int* argi, GITypeInfo* info, GIDirection dir,
                gpointer* arg_ptr, union typebox* arg_val)
{
  int argi_advance = 0;
  *arg_ptr = arg_val;
  switch (g_type_info_get_tag(info))
    {
#define TYPE_CASE(tag, type, expr)                                      \
      case GI_TYPE_TAG_ ## tag :                                        \
        if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT)        \
          {                                                             \
            arg_val->type ## _ = (type) expr;                           \
            argi_advance = 1;                                           \
          }                                                             \
        break

      TYPE_CASE(BOOLEAN, gboolean, lua_toboolean(L, *argi));
      TYPE_CASE(INT8, gint8, luaL_checkinteger(L, *argi));
      TYPE_CASE(UINT8, guint8, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT16, gint16, luaL_checkinteger(L, *argi));
      TYPE_CASE(UINT16, guint16, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT32, guint32, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT64, gint64, luaL_checkinteger(L, *argi));
      TYPE_CASE(UINT64, guint64, luaL_checkinteger(L, *argi));
      TYPE_CASE(SHORT, gshort, luaL_checkinteger(L, *argi));
      TYPE_CASE(USHORT, gushort, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT, gint, luaL_checkinteger(L, *argi));
      TYPE_CASE(UINT, guint, luaL_checkinteger(L, *argi));
      TYPE_CASE(LONG, glong, luaL_checkinteger(L, *argi));
      TYPE_CASE(ULONG, gulong, luaL_checkinteger(L, *argi));
      TYPE_CASE(SSIZE, gssize, luaL_checkinteger(L, *argi));
      TYPE_CASE(SIZE, gsize, luaL_checkinteger(L, *argi));
      TYPE_CASE(FLOAT, gfloat, luaL_checkinteger(L, *argi));
      TYPE_CASE(DOUBLE, gdouble, luaL_checkinteger(L, *argi));
      TYPE_CASE(GTYPE, GType, luaL_checkinteger(L, *argi));

#undef TYPE_CASE

    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      {
        gpointer str;
        if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT)
          {
            str = (gpointer)luaL_checkstring(L, *argi);
            argi_advance = 1;
          }

        if (dir == GI_DIRECTION_IN)
          *arg_ptr = str;
        else if (dir == GI_DIRECTION_INOUT)
          arg_val->gpointer_ = str;
      }
      break;

    default:
      /* TODO: Handle the complex ones. */
      break;
    }
}

static int
function_call(lua_State* L)
{
  gint argc, flags, has_self, throws, argi, lua_argi, ti_argi, ffi_argi;
  gpointer* args_ptr;
  union typebox* args_val;
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  GError* err = NULL;
  GITypeInfo* ti;

  /* If function is a method, it has implicit 'self' parameter. */
  flags = g_function_info_get_flags(function->info);
  has_self = (flags & GI_FUNCTION_IS_METHOD) != 0 &&
    (flags & GI_FUNCTION_IS_CONSTRUCTOR) == 0;
  throws = (flags & GI_FUNCTION_THROWS) != 0;
  argc = g_callable_info_get_n_args(function->info);

  /* Allocate array for arguments. */
  args_ptr = g_newa(gpointer, argc + 1 + has_self + throws);
  args_val = g_newa(union typebox, argc + 1 + has_self + throws);

  lua_argi = 2;
  ffi_argi = 0;
  ti_argi = 0;

  /* Handle return value. */
  ti = g_callable_info_get_return_type(function->info);
  function_arg_in(L, &lua_argi, ti, GI_DIRECTION_OUT,
                  &args_ptr[0], &args_val[0]);
  g_base_info_unref(ti);

  /* Handle 'self', if the function has it. */
  if (has_self)
    {
      ti = g_base_info_get_container(function->info);
      function_arg_in(L, &lua_argi, ti, GI_DIRECTION_IN,
                      &args_ptr[1], &args_val[1]);
      g_base_info_unref(ti);
      ffi_argi++;
    }

  /* Handle ordinary parameters. */
  for (argi = 0; argi < argc; argi++)
    {
      GIArgInfo* ai = g_callable_info_get_arg(function->info, ti_argi++);
      ti = g_arg_info_get_type(ai);
      function_arg_in(L, &lua_argi, ti, g_arg_info_get_direction(ai),
                      &args_ptr[ffi_argi], &args_val[ffi_argi]);
      ffi_argi++;
      g_base_info_unref(ti);
      g_base_info_unref(ai);
    }

  /* Handle 'throws' parameter, if function does it. */
  if (throws)
    args_ptr[ffi_argi++] = &err;

  /* Perform the call. */
  ffi_call(&function->invoker.cif, function->invoker.native_address,
           args_ptr[0], &args_ptr[1]);

  /* Check, whether an error happened. */
  if (throws && err != 0)
    return lgi_error(L, err);

  return 0;
}

static const struct luaL_reg function_reg[] = {
  { "__gc", function_gc },
  { "__call", function_call },
  { NULL, NULL }
};

/*
   lgi._get(namespace, symbolname)
*/
static int
lgi_get(lua_State* L)
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
          function_new(L, (GIFunctionInfo*) info);
          return 1;

        default:
          lua_pushinteger(L, type);
          return 1;
        }
    }

  /* If the symbol was not handled inside the switch above, it does not
     exist. */
  lua_pushnil(L);
  return 1;
}

static const struct luaL_reg lgi_reg[] = {
  { "_get", lgi_get },
  { NULL, NULL }
};

int
luaopen_lgi(lua_State* L)
{
  g_type_init();
  luaL_newmetatable(L, UD_FUNCTION);
  luaL_register(L, NULL, function_reg);
  lua_pop(L, 1);
  luaL_register(L, "lgi", lgi_reg);
  lua_gettop(L);
  return 1;
}
