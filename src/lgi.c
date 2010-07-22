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
  function->info = g_base_info_ref(info);
  if (!g_function_info_prep_invoker(info, &function->invoker, &err))
    lgi_throw(L, err);
}

static int
function_gc(lua_State* L)
{
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  g_function_invoker_destroy(&function->invoker);
  g_base_info_unref(function->info);
  return 0;
}

static int
function_tostring(lua_State* L)
{
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  lua_pushfstring(L, "lgifun: %s.%s %p",
                  g_base_info_get_namespace(function->info),
                  g_base_info_get_name(function->info), function);
  return 1;
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

typedef void
(*function_arg)(lua_State* L, int* argi, GITypeInfo* info,
                GIDirection dir, union typebox*);

static void
function_arg_in(lua_State* L, int* argi, GITypeInfo* info, GIDirection dir,
		union typebox* arg)
{
  int arg_used = 0;
  switch (g_type_info_get_tag(info))
    {
#define TYPE_CASE(tag, type, expr)					\
      case GI_TYPE_TAG_ ## tag :					\
	if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT)	\
	  {								\
	    arg->type ## _ = (type)expr;				\
	    arg_used = 1;						\
	  }								\
	break

      TYPE_CASE(BOOLEAN, gboolean, lua_toboolean(L, *argi));
      TYPE_CASE(INT8, gint8, luaL_checkinteger(L, *argi));
      TYPE_CASE(UINT8, guint8, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT16, gint16, luaL_checkinteger(L, *argi));
      TYPE_CASE(UINT16, guint16, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT32, guint32, luaL_checkinteger(L, *argi));
      TYPE_CASE(INT64, gint64, luaL_checknumber(L, *argi));
      TYPE_CASE(UINT64, guint64, luaL_checknumber(L, *argi));
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
      TYPE_CASE(UTF8, gpointer, luaL_checkstring(L, *argi));
      TYPE_CASE(FILENAME, gpointer, luaL_checkstring(L, *argi));

#undef TYPE_CASE

    case GI_TYPE_TAG_VOID:
      break;

    default:
      /* TODO: Handle the complex ones. */
      break;
    }

  *argi += arg_used;
}

static void
function_arg_out(lua_State* L, int* argi, GITypeInfo* info, GIDirection dir,
                 union typebox* arg)
{
  int arg_used = 0;
  switch (g_type_info_get_tag(info))
    {
#define TYPE_CASE(tag, type, push)					\
      case GI_TYPE_TAG_ ## tag :					\
	if (dir == GI_DIRECTION_OUT || dir == GI_DIRECTION_INOUT)	\
	  {								\
            push(L, arg->type ## _);                                    \
	    arg_used = 1;						\
	  }								\
	break

      TYPE_CASE(BOOLEAN, gboolean, lua_pushboolean);
      TYPE_CASE(INT8, gint8, lua_pushinteger);
      TYPE_CASE(UINT8, guint8, lua_pushinteger);
      TYPE_CASE(INT16, gint16, lua_pushinteger);
      TYPE_CASE(UINT16, guint16, lua_pushinteger);
      TYPE_CASE(INT32, guint32, lua_pushinteger);
      TYPE_CASE(INT64, gint64, lua_pushnumber);
      TYPE_CASE(UINT64, guint64, lua_pushnumber);
      TYPE_CASE(SHORT, gshort, lua_pushinteger);
      TYPE_CASE(USHORT, gushort, lua_pushinteger);
      TYPE_CASE(INT, gint, lua_pushinteger);
      TYPE_CASE(UINT, guint, lua_pushinteger);
      TYPE_CASE(LONG, glong, lua_pushinteger);
      TYPE_CASE(ULONG, gulong, lua_pushinteger);
      TYPE_CASE(SSIZE, gssize, lua_pushinteger);
      TYPE_CASE(SIZE, gsize, lua_pushinteger);
      TYPE_CASE(FLOAT, gfloat, lua_pushinteger);
      TYPE_CASE(DOUBLE, gdouble, lua_pushinteger);
      TYPE_CASE(GTYPE, GType, lua_pushinteger);
      TYPE_CASE(UTF8, gpointer, lua_pushstring);
      TYPE_CASE(FILENAME, gpointer, lua_pushstring);

#undef TYPE_CASE

    case GI_TYPE_TAG_VOID:
      break;

    default:
      /* TODO: Handle the complex ones. */
      break;
    }

  *argi += arg_used;
}

static int
function_handle_args(lua_State* L, function_arg do_arg, GICallableInfo* fi,
                     int has_self, int throws, int argc, union typebox* args)
{
  gint argi, lua_argi = 2, ti_argi = 0, ffi_argi = 1;
  GITypeInfo* ti;

  /* Handle return value. */
  ti = g_callable_info_get_return_type(fi);
  do_arg(L, &lua_argi, ti, GI_DIRECTION_OUT, &args[0]);
  g_base_info_unref(ti);

  /* Handle 'self', if the function has it. */
  if (has_self)
    {
      ti = g_base_info_get_container(fi);
      do_arg(L, &lua_argi, ti, GI_DIRECTION_IN, &args[1]);
      g_base_info_unref(ti);
      ffi_argi++;
    }

  /* Handle ordinary parameters. */
  ti_argi = 0;
  for (argi = 0; argi < argc; argi++)
    {
      GIArgInfo* ai = g_callable_info_get_arg(fi, ti_argi++);
      ti = g_arg_info_get_type(ai);
      do_arg(L, &lua_argi, ti, g_arg_info_get_direction(ai), &args[ffi_argi++]);
      g_base_info_unref(ti);
      g_base_info_unref(ai);
    }

  return lua_argi - 2;
}

static int
function_call(lua_State* L)
{
  gint i, argc, argffi, flags, has_self, throws;
  gpointer* args_ptr;
  union typebox* args_val;
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  GError* err = NULL;

  /* Check general function characteristics. */
  flags = g_function_info_get_flags(function->info);
  has_self = (flags & GI_FUNCTION_IS_METHOD) != 0 &&
    (flags & GI_FUNCTION_IS_CONSTRUCTOR) == 0;
  throws = (flags & GI_FUNCTION_THROWS) != 0;
  argc = g_callable_info_get_n_args(function->info);

  /* Allocate array for arguments. */
  argffi = argc + 1 + has_self + throws;
  args_val = g_newa(union typebox, argffi);
  args_ptr = g_newa(gpointer, argffi);
  for (i = 0; i < argffi; ++i)
    args_ptr[i] = &args_val[i];

  /* Process parameters for input. */
  function_handle_args(L, function_arg_in, function->info, has_self, throws,
                       argc, args_val);

  /* Handle 'throws' parameter, if function does it. */
  if (throws)
    args_val[argffi - 1].gpointer_ = &err;

  /* Perform the call. */
  ffi_call(&function->invoker.cif, function->invoker.native_address,
	   args_ptr[0], &args_ptr[1]);

  /* Check, whether function threw. */
  if (err != NULL)
    return lgi_error(L, err);

  /* Process parameters for output. */
  return function_handle_args(L, function_arg_out, function->info, has_self,
                              throws, argc, args_val);
}

static const struct luaL_reg function_reg[] = {
  { "__gc", function_gc },
  { "__call", function_call },
  { "__tostring", function_tostring },
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
  if (info == NULL)
    {
      lua_pushboolean(L, 0);
      lua_pushfstring(L, "symbol %s.%s not found", namespace_, symbol);
      return 2;
    }

  /* Check the type of the symbol. */
  type = g_base_info_get_type(info);
  switch (type)
    {
    case GI_INFO_TYPE_FUNCTION:
      function_new(L, (GIFunctionInfo*)info);
      break;

    default:
      lua_pushinteger(L, type);
      break;
    }

  g_base_info_unref(info);
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
