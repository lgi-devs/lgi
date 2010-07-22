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

static int
lgi_val_to_lua(lua_State* L, GITypeInfo* ti, GArgument* val)
{
  int pushed = 1;
  switch (g_type_info_get_tag(ti))
    {
      /* Simple (native) types. */
#define TYPE_CASE(tag, type, member, push)      \
      case GI_TYPE_TAG_ ## tag:                 \
        push(L, val->member);                   \
	break

      TYPE_CASE(BOOLEAN, gboolean, v_boolean, lua_pushboolean);
      TYPE_CASE(INT8, gint8, v_int8, lua_pushinteger);
      TYPE_CASE(UINT8, guint8, v_uint8, lua_pushinteger);
      TYPE_CASE(INT16, gint16, v_int16, lua_pushinteger);
      TYPE_CASE(UINT16, guint16, v_uint16, lua_pushinteger);
      TYPE_CASE(INT32, gint32, v_int32, lua_pushinteger);
      TYPE_CASE(UINT32, guint32, v_uint32, lua_pushinteger);
      TYPE_CASE(INT64, gint64, v_int64, lua_pushnumber);
      TYPE_CASE(UINT64, guint64, v_uint64, lua_pushnumber);
      TYPE_CASE(FLOAT, gfloat, v_float, lua_pushinteger);
      TYPE_CASE(DOUBLE, gdouble, v_double, lua_pushinteger);
      TYPE_CASE(SHORT, gshort, v_short, lua_pushinteger);
      TYPE_CASE(USHORT, gushort, v_ushort, lua_pushinteger);
      TYPE_CASE(INT, gint, v_int, lua_pushinteger);
      TYPE_CASE(UINT, guint, v_uint, lua_pushinteger);
      TYPE_CASE(LONG, glong, v_long, lua_pushinteger);
      TYPE_CASE(ULONG, gulong, v_ulong, lua_pushinteger);
      TYPE_CASE(SSIZE, gssize, v_ssize, lua_pushinteger);
      TYPE_CASE(SIZE, gsize, v_size, lua_pushinteger);
      TYPE_CASE(GTYPE, GType, v_long, lua_pushinteger);
      TYPE_CASE(UTF8, gpointer, v_pointer, lua_pushstring);
      TYPE_CASE(FILENAME, gpointer, v_pointer, lua_pushstring);

#undef TYPE_CASE

      /* TODO: Handle the complex ones. */

    default:
      pushed = 0;
      break;
    }

  return pushed;
}

static int
lgi_val_from_lua(lua_State* L, int index, GITypeInfo* ti, GArgument* val)
{
  int received = 1;
  switch (g_type_info_get_tag(ti))
    {
#define TYPE_CASE(tag, type, member, expr)      \
      case GI_TYPE_TAG_ ## tag :                \
        val->member = (type)expr;               \
	break

      TYPE_CASE(BOOLEAN, gboolean, v_boolean, lua_toboolean(L, index));
      TYPE_CASE(INT8, gint8, v_int8, luaL_checkinteger(L, index));
      TYPE_CASE(UINT8, guint8, v_uint8, luaL_checkinteger(L, index));
      TYPE_CASE(INT16, gint16, v_int16, luaL_checkinteger(L, index));
      TYPE_CASE(UINT16, guint16, v_uint16, luaL_checkinteger(L, index));
      TYPE_CASE(INT32, gint32, v_int32, luaL_checkinteger(L, index));
      TYPE_CASE(UINT32, guint32, v_uint32, luaL_checkinteger(L, index));
      TYPE_CASE(INT64, gint64, v_int64, luaL_checknumber(L, index));
      TYPE_CASE(UINT64, guint64, v_uint64, luaL_checknumber(L, index));
      TYPE_CASE(FLOAT, gfloat, v_float, luaL_checkinteger(L, index));
      TYPE_CASE(DOUBLE, gdouble, v_double, luaL_checkinteger(L, index));
      TYPE_CASE(SHORT, gshort, v_short, luaL_checkinteger(L, index));
      TYPE_CASE(USHORT, gushort, v_ushort, luaL_checkinteger(L, index));
      TYPE_CASE(INT, gint, v_int, luaL_checkinteger(L, index));
      TYPE_CASE(UINT, guint, v_uint, luaL_checkinteger(L, index));
      TYPE_CASE(LONG, glong, v_long, luaL_checkinteger(L, index));
      TYPE_CASE(ULONG, gulong, v_ulong, luaL_checkinteger(L, index));
      TYPE_CASE(SSIZE, gssize, v_ssize, luaL_checkinteger(L, index));
      TYPE_CASE(SIZE, gsize, v_size, luaL_checkinteger(L, index));
      TYPE_CASE(GTYPE, GType, v_long, luaL_checkinteger(L, index));
      TYPE_CASE(UTF8, gpointer, v_pointer, luaL_checkstring(L, index));
      TYPE_CASE(FILENAME, gpointer, v_pointer, luaL_checkstring(L, index));

#undef TYPE_CASE

      /* TODO: Handle the complex ones. */

    default:
      received = 0;
      break;
    }

  return received;
}

/* 'struct' userdata: wraps structure with its typeinfo. */
struct ud_struct
{
    GIStructInfo* info;
    gpointer addr;
};
#define UD_STRUCT "lgi.struct"

static int
struct_new(lua_State* L, GIStructInfo* info, gpointer addr)
{
    if (addr != NULL)
      {
        struct ud_struct* struct_ =
          lua_newuserdata(L, sizeof(struct ud_struct));
        luaL_getmetatable(L, UD_STRUCT);
        lua_setmetatable(L, -2);
        struct_->info = g_base_info_ref(info);
        struct_->addr = addr;
      }
    else
      lua_pushnil(L);

    return 1;
};

static int
struct_gc(lua_State* L)
{
  struct ud_struct* struct_ = luaL_checkudata(L, 1, UD_STRUCT);
  g_base_info_unref(struct_->info);
  return 0;
}

static int
struct_tostring(lua_State* L)
{
  struct ud_struct* struct_ = luaL_checkudata(L, 1, UD_STRUCT);
  lua_pushfstring(L, "lgistruct: %s.%s %p",
                  g_base_info_get_namespace(struct_->info),
                  g_base_info_get_name(struct_->info), struct_);
  return 1;
}

static int
struct_index(lua_State* L)
{
  return 0;
}

static int
struct_newindex(lua_State* L)
{
  return 0;
}

static const struct luaL_reg struct_reg[] = {
  { "__gc", struct_gc },
  { "__index", struct_index },
  { "__newindex", struct_newindex },
  { "__tostring", struct_tostring },
  { NULL, NULL }
};

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

typedef void
(*function_arg)(lua_State* L, int* argi, GITypeInfo* info,
                GIDirection dir, GArgument*);

static void
function_arg_in(lua_State* L, int* argi, GITypeInfo* info, GIDirection dir,
		GArgument* arg)
{
  if (dir == GI_DIRECTION_IN || dir == GI_DIRECTION_INOUT)
    *argi += lgi_val_from_lua(L, *argi, info, arg);
}

static void
function_arg_out(lua_State* L, int* argi, GITypeInfo* info, GIDirection dir,
                 GArgument* arg)
{
  if (dir == GI_DIRECTION_OUT || dir == GI_DIRECTION_INOUT)
    *argi += lgi_val_to_lua(L, info, arg);
}

static int
function_handle_args(lua_State* L, function_arg do_arg, GICallableInfo* fi,
                     int has_self, int throws, int argc, GArgument* args)
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
  GArgument* args_val;
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
  args_val = g_newa(GArgument, argffi);
  args_ptr = g_newa(gpointer, argffi);
  for (i = 0; i < argffi; ++i)
    args_ptr[i] = &args_val[i];

  /* Process parameters for input. */
  function_handle_args(L, function_arg_in, function->info, has_self, throws,
                       argc, args_val);

  /* Handle 'throws' parameter, if function does it. */
  if (throws)
    args_val[argffi - 1].v_pointer = &err;

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

static void
lgi_reg_udata(lua_State* L, const struct luaL_reg* reg, const char* meta)
{
  luaL_newmetatable(L, meta);
  luaL_register(L, NULL, reg);
  lua_pop(L, 1);
}

int
luaopen_lgi(lua_State* L)
{
  g_type_init();
  lgi_reg_udata(L, struct_reg, UD_STRUCT);
  lgi_reg_udata(L, function_reg, UD_FUNCTION);
  luaL_register(L, "lgi", lgi_reg);
  return 1;
}
