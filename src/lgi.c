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

#include <glib.h>
#include <girepository.h>
#include <girffi.h>

/* Key in registry, containing table with al our private data. */
static int lgi_regkey;
#define LGI_REG_CACHE 1

static int function_new(lua_State* L, GIFunctionInfo* info);
static int struct_new(lua_State* L, GIStructInfo* info, gpointer addr,
		      gboolean owns);

/* 'struct' userdata: wraps structure with its typeinfo. */
struct ud_struct
{
  GIStructInfo* info;
  gpointer addr;
  gboolean owns;
};
#define UD_STRUCT "lgi.struct"

/* 'function' userdata: wraps function prepared to be called through ffi. */
struct ud_function
{
  GIFunctionInvoker invoker;
  GIFunctionInfo* info;
};
#define UD_FUNCTION "lgi.function"

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

/* Stores object represented by specified gpointer from the cache to
   the stack.  If not found in the cache, returns 0 and stores
   nothing. */
static int
lgi_get_cached(lua_State* L, gpointer obj)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_CACHE);
  lua_pushlightuserdata(L, obj);
  lua_rawget(L, -2);
  lua_replace(L, -3);
  lua_pop(L, 1);
  if (lua_isnil(L, -1))
    {
      lua_pop(L, 1);
      return 0;
    }

  return 1;
}

/* Stores object into specified cache. */
static void
lgi_set_cached(lua_State* L, gpointer obj)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_CACHE);
  lua_pushlightuserdata(L, obj);
  lua_pushvalue(L, -4);
  lua_rawset(L, -3);
  lua_pop(L, 2);
}

static int
lgi_simple_val_to_lua(lua_State* L, GITypeTag tag, GArgument* val)
{
  int pushed = 1;
  switch (tag)
    {
      /* Simple (native) types. */
#define TYPE_CASE(tag, type, member, push)	\
      case GI_TYPE_TAG_ ## tag:			\
	push(L, val->member);			\
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

    default:
      pushed = 0;
    }

  return pushed;
}

static int
lgi_val_to_lua(lua_State* L, GITypeInfo* ti, GArgument* val, gboolean owned)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  int pushed = lgi_simple_val_to_lua(L, tag, val);
  if (pushed == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_INTERFACE:
	  /* Interface types.  Get the interface type and switch according
	     to the real type. */
	  {
	    GIBaseInfo* ii = g_type_info_get_interface(ti);
	    switch (g_base_info_get_type(ii))
	      {
	      case GI_INFO_TYPE_ENUM:
		/* Resolve enum to the real value. */
		pushed =
		  lgi_simple_val_to_lua(L, g_enum_info_get_storage_type(ii),
					val);
		break;

	      case GI_INFO_TYPE_STRUCT:
		/* Create/Get struct object. */
		pushed = struct_new(L, ii, val->v_pointer, owned);
		break;

	      default:
		pushed = 0;
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	default:
	  pushed = 0;
	}
    }

  return pushed;
}

static int
lgi_val_from_lua(lua_State* L, int index, GITypeInfo* ti, GArgument* val,
		 gboolean optional)
{
  int received = 1;
  switch (g_type_info_get_tag(ti))
    {
#define TYPE_CASE(tag, type, member, expr)                      \
      case GI_TYPE_TAG_ ## tag :                                \
	val->member = (type)((optional &&                       \
			      lua_isnoneornil(L, index)) ?      \
			     0 : expr);                         \
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

    case GI_TYPE_TAG_INTERFACE:
      /* Interface types.  Get the interface type and switch according
	 to the real type. */
      {
	GIBaseInfo* ii = g_type_info_get_interface(ti);
	switch (g_base_info_get_type(ii))
	  {
	  case GI_INFO_TYPE_STRUCT:
	    if (optional && lua_isnoneornil(L, index))
	      val->v_pointer = 0;
	    else
	      {
		struct ud_struct* struct_ =
		  luaL_checkudata(L, index, UD_STRUCT);
		val->v_pointer = struct_->addr;
	      }
	    break;

	  default:
	    received = 0;
	  }
	g_base_info_unref(ii);
      }
      break;

    default:
      received = 0;
      break;
    }

  return received;
}

/* Allocates/initializes specified object (if applicable), stores it
   on the stack. */
static int
lgi_type_new(lua_State* L, GIBaseInfo* ii, GArgument* val)
{
  int vals = 0;
  switch (g_base_info_get_type(ii))
    {
    case GI_INFO_TYPE_FUNCTION:
      vals = function_new(L, ii);
      break;

    case GI_INFO_TYPE_STRUCT:
      val->v_pointer = g_malloc0(g_struct_info_get_size(ii));
      vals = struct_new(L, ii, val->v_pointer, TRUE);
      break;

    case GI_INFO_TYPE_CONSTANT:
      {
	GITypeInfo* ti = g_constant_info_get_type(ii);
	GArgument val;
	g_constant_info_get_value(ii, &val);
	vals = lgi_val_to_lua(L, ti, &val, FALSE);
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
static int
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
struct_new(lua_State* L, GIStructInfo* info, gpointer addr, gboolean owns)
{
    if (addr != NULL)
      {
	/* Check, whether struct is already in the cache. */
	if (lgi_get_cached(L, addr) == 0)
	  {
	    struct ud_struct* struct_ =
	      lua_newuserdata(L, sizeof(struct ud_struct));
	    luaL_getmetatable(L, UD_STRUCT);
	    lua_setmetatable(L, -2);
	    struct_->info = g_base_info_ref(info);
	    struct_->addr = addr;
	    struct_->owns = owns;
	    lgi_set_cached(L, addr);
	  }
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
  if (struct_->owns)
    g_free(struct_->addr);
  return 0;
}

static int
struct_tostring(lua_State* L)
{
  int n;
  struct ud_struct* struct_ = luaL_checkudata(L, 1, UD_STRUCT);
  lua_pushstring(L, "lgi-struct: ");
  n = lgi_type_get_name(L, struct_->info);
  lua_pushfstring(L, " %p", struct_);
  lua_concat(L, n + 2);
  return 1;
}

static GIFieldInfo*
struct_find_field(lua_State* L, GIStructInfo* si, const gchar* name)
{
  GIFieldInfo* fi = NULL;
  int i;
  for (i = 0; i < g_struct_info_get_n_fields(si); i++)
    {
      fi = g_struct_info_get_field(si, i);
      g_assert(fi != NULL);
      if (g_strcmp0(g_base_info_get_name(fi), name) == 0)
	break;

      g_base_info_unref(fi);
      fi = NULL;
    }

  return fi;
}

static int
struct_index(lua_State* L)
{
  struct ud_struct* struct_ = luaL_checkudata(L, 1, UD_STRUCT);
  const gchar* name = luaL_checkstring(L, 2);
  int vals;
  GIFieldInfo* fi;
  GITypeInfo* ti;
  GArgument* val;

  /* Find the field. */
  fi = struct_find_field(L, struct_->info, name);
  if (fi == NULL)
    return luaL_error(L, "struct %s.%s: no '%s'",
		      g_base_info_get_namespace(struct_->info),
		      g_base_info_get_name(struct_->info), name);

  /* Check, whether the field is readable. */
  if ((g_field_info_get_flags(fi) & GI_FIELD_IS_READABLE) == 0)
    {
      g_base_info_unref(fi);
      return luaL_error(L, "struct %s.%s: '%s' not readable",
			g_base_info_get_namespace(struct_->info),
			g_base_info_get_name(struct_->info), name);
    }

  /* Read the field. */
  val = G_STRUCT_MEMBER_P(struct_->addr, g_field_info_get_offset(fi));
  ti = g_field_info_get_type(fi);
  vals = lgi_val_to_lua(L, ti, val, FALSE);
  g_base_info_unref(ti);
  g_base_info_unref(fi);
  return vals;
}

static int
struct_newindex(lua_State* L)
{
  struct ud_struct* struct_ = luaL_checkudata(L, 1, UD_STRUCT);
  const gchar* name = luaL_checkstring(L, 2);
  int vals;
  GIFieldInfo* fi;
  GITypeInfo* ti;
  GArgument* val;

  /* Find the field. */
  fi = struct_find_field(L, struct_->info, name);
  if (fi == NULL)
    return luaL_error(L, "struct %s.%s: no '%s'",
		      g_base_info_get_namespace(struct_->info),
		      g_base_info_get_name(struct_->info), name);

  /* Check, whether the field is readable. */
  if ((g_field_info_get_flags(fi) & GI_FIELD_IS_WRITABLE) == 0)
    {
      g_base_info_unref(fi);
      return luaL_error(L, "struct %s.%s: '%s' not writable",
			g_base_info_get_namespace(struct_->info),
			g_base_info_get_name(struct_->info), name);
    }

  /* Write the field. */
  val = G_STRUCT_MEMBER_P(struct_->addr, g_field_info_get_offset(fi));
  ti = g_field_info_get_type(fi);
  vals = lgi_val_from_lua(L, 3, ti, val, FALSE);
  g_base_info_unref(ti);
  g_base_info_unref(fi);
  return vals;
}

static const struct luaL_reg struct_reg[] = {
  { "__gc", struct_gc },
  { "__index", struct_index },
  { "__newindex", struct_newindex },
  { "__tostring", struct_tostring },
  { NULL, NULL }
};

static int
function_new(lua_State* L, GIFunctionInfo* info)
{
  GError* err = NULL;
  struct ud_function* function = lua_newuserdata(L, sizeof(struct ud_function));
  luaL_getmetatable(L, UD_FUNCTION);
  lua_setmetatable(L, -2);
  function->info = g_base_info_ref(info);
  if (!g_function_info_prep_invoker(info, &function->invoker, &err))
    lgi_throw(L, err);

  /* Check, whether such function is not already present in the cache.
     If it is, use the one we already have. */
  if (lgi_get_cached(L, function->invoker.native_address) == 1)
    /* Replace with previously created function. */
    lua_replace(L, -2);
  else
    /* Store new function into the cache. */
    lgi_set_cached(L, function->invoker.native_address);

  return 1;
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
  int n;
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  lua_pushstring(L, "lgi-functn: ");
  n = lgi_type_get_name(L, function->info);
  lua_pushfstring(L, " %p", function);
  lua_concat(L, n + 2);
  return 1;
}

static int
function_call(lua_State* L)
{
  gint i, argc, argffi, flags, has_self, throws, lua_argi, ti_argi, ffi_argi;
  gpointer* args_ptr;
  GError* err = NULL;
  struct ud_function* function = luaL_checkudata(L, 1, UD_FUNCTION);
  struct arginfo
  {
    GArgument arg;
    GIArgInfo ai;
    GITypeInfo ti;
    GIDirection dir;
  } *args;

  /* Check general function characteristics. */
  flags = g_function_info_get_flags(function->info);
  has_self = (flags & GI_FUNCTION_IS_METHOD) != 0 &&
    (flags & GI_FUNCTION_IS_CONSTRUCTOR) == 0;
  throws = (flags & GI_FUNCTION_THROWS) != 0;
  argc = g_callable_info_get_n_args(function->info);

  /* Allocate array for arguments. */
  argffi = argc + 1 + has_self + throws;
  args = g_newa(struct arginfo, argffi);
  args_ptr = g_newa(gpointer, argffi);
  for (i = 0; i < argffi; ++i)
    args_ptr[i] = &args[i].arg;

  /* Process parameters for input. */
  lua_argi = 2;
  ffi_argi = 1;
  ti_argi = 0;
  if (has_self)
    {
      /* TODO: 'self' handling: check for object type and marshall it
	 in from lua. */
      lua_argi++;
      args[1].arg.v_pointer = NULL;
      ffi_argi++;
    }

  /* Handle parameters. */
  for (i = 0; i < argc; i++, ffi_argi++)
    {
      g_callable_info_load_arg(function->info, ti_argi++, &args[ffi_argi].ai);
      g_arg_info_load_type(&args[ffi_argi].ai, &args[ffi_argi].ti);
      args[ffi_argi].dir = g_arg_info_get_direction(&args[ffi_argi].ai);
      if (args[ffi_argi].dir == GI_DIRECTION_IN ||
	  args[ffi_argi].dir == GI_DIRECTION_INOUT)
	lua_argi +=
	  lgi_val_from_lua(L, lua_argi, &args[ffi_argi].ti, &args[ffi_argi].arg,
			   g_arg_info_is_optional(&args[ffi_argi].ai) ||
			   g_arg_info_may_be_null(&args[ffi_argi].ai));
      else if (g_arg_info_is_caller_allocates(&args[ffi_argi].ai))
	{
	  /* Allocate target space. */
	  GIBaseInfo* ii = g_type_info_get_interface(&args[ffi_argi].ti);
	  lgi_type_new(L, ii, &args[ffi_argi].arg);
	  g_base_info_unref(ii);
	}
    }

  /* Handle 'throws' parameter, if function does it. */
  if (throws)
    args[ffi_argi].arg.v_pointer = &err;

  /* Perform the call. */
  ffi_call(&function->invoker.cif, function->invoker.native_address,
	   args_ptr[0], &args_ptr[1]);

  /* Check, whether function threw. */
  if (err != NULL)
    return lgi_error(L, err);

  /* Process parameters for output. */
  lua_argi = 0;
  ffi_argi = 0;
  ti_argi = 0;

  /* Handle return value. */
  g_callable_info_load_return_type(function->info, &args[0].ti);
  lua_argi += lgi_val_to_lua(L, &args[0].ti, &args[0].arg, FALSE);

  /* Handle parameters. */
  for (i = 0; i < argc; i++, ffi_argi++)
    {
      if (args[ffi_argi].dir == GI_DIRECTION_OUT ||
	  args[ffi_argi].dir == GI_DIRECTION_INOUT)
	lua_argi +=
	  lgi_val_to_lua(L, &args[ffi_argi].ti, &args[ffi_argi].arg,
			 g_arg_info_is_caller_allocates(&args[ffi_argi].ai));
    }

  return lua_argi;
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
  const gchar* symbol = luaL_checkstring(L, 2);\
  const gchar* fname = NULL;
  GIBaseInfo* info;
  GArgument unused;
  int vals = 0;

  /* Make sure that the repository is loaded. */
  if (g_irepository_require(NULL, namespace_, NULL, 0, &err) == NULL)
    return lgi_error(L, err);

  /* Get information about the symbol. */
  info = g_irepository_find_by_name(NULL, namespace_, symbol);

  /* In case that container was specified, look the symbol up in it. */
  if (g_base_info_get_type(info) == GI_INFO_TYPE_OBJECT &&
      !lua_isnoneornil(L, 3))
    {
      fname = luaL_checkstring(L, 3);
      GIBaseInfo* fi = g_object_info_find_method(info, fname);
      g_base_info_unref(info);
      info = fi;
    }

  if (info == NULL)
    {
      lua_pushboolean(L, 0);
      lua_pushfstring(L, "symbol %s.%s%s%s not found", namespace_, symbol,
		      fname ? "." : "", fname ? fname : "");
      return 2;
    }

  /* Create new instance of the specified type. */
  vals = lgi_type_new(L, info, &unused);
  g_base_info_unref(info);
  return 1;
}

static const struct luaL_reg lgi_reg[] = {
  { "get", lgi_get },
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
luaopen_lgi__core(lua_State* L)
{
  /* GLib initializations. */
  g_type_init();

  /* Register userdata types. */
  lgi_reg_udata(L, struct_reg, UD_STRUCT);
  lgi_reg_udata(L, function_reg, UD_FUNCTION);

  /* Prepare registry table (avoid polluting global registry, make
     private table in it instead.*/
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lgi_regkey = luaL_ref(L, LUA_REGISTRYINDEX);

  /* Create object cache, which has weak values. */
  lua_newtable(L);
  lua_pushstring(L, "__mode");
  lua_pushstring(L, "v");
  lua_rawset(L, -3);
  lua_newtable(L);
  lua_pushvalue(L, -2);
  lua_setmetatable(L, -2);
  lua_rawseti(L, -3, LGI_REG_CACHE);
  lua_pop(L, 2);

  /* Register _core interface. */
  luaL_register(L, "lgi._core", lgi_reg);

  /* In debug version, make our private registry browsable. */
#ifndef NDEBUG
  lua_pushstring(L, "reg");
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawset(L, -3);
#endif

  return 1;
}
