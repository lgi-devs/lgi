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
#include <glib/gprintf.h>
#include <glib-object.h>
#include <girepository.h>
#include <girffi.h>

/* Key in registry, containing table with al our private data. */
static int lgi_regkey;
enum lgi_reg
{
  LGI_REG_CACHE = 1,
  LGI_REG_REPO = 2,
  LGI_REG__LAST
};

/* GIBaseInfo of GIBaseInfo type itself.  Leaks, never freed. */
GIBaseInfo* lgi_baseinfo_info;

/* Creates new userdata representing instance of struct/object
   described by 'ii'.  Transfer describes, whether the
   ownership is transferred and gc method releases the object.	The
   special transfer value is GI_TRANSFER_CONTAINER, which means that
   the structure is allocated and its address is put into addr
   (i.e. addr parameter is output in this case). */
static int compound_new(lua_State* L, GIBaseInfo* ii, gpointer* addr,
			GITransfer transfer);

/* Retrieves compound-type parameter from given Lua-stack position, checks,
   whether it is suitable for requested ii type.  Returns pointer to the
   compound object, returns NULL if Lua-stack value is nil and optional is
   TRUE. */
static gpointer compound_get(lua_State* L, int arg, GIBaseInfo* ii,
			     gboolean optional);

/* Creates new userdata representing instance of function described by
   'info'. Parses function signature and might report an error if the
   function cannot be wrapped by lgi.  In any case, returns number of
   items pushed to the stack.*/
static int function_new(lua_State* L, GIFunctionInfo* info);

/* 'compound' userdata: wraps compound with reference to its repo table. */
struct ud_compound
{
  /* Address of the structure data. */
  gpointer addr;

  /* Lua reference to repo table representing this compound. */
  int ref_repo : 31;

  /* Flag indicating whether compound is owned. */
  int owns : 1;

  /* If the structure is allocated 'on the stack', its data is here. */
  gchar data[1];
};
#define UD_COMPOUND "lgi.compound"

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
  g_assert(err != NULL);
  lua_pushfstring(L, "%s (%d)", err->message, err->code);
  g_error_free(err);
  return luaL_error(L, "%s", lua_tostring(L, -1));
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

/* Returns size in bytes of given type/value. */
static gsize
lgi_type_get_size(GITypeTag tag)
{
  gsize size;
  switch (tag)
    {
#define TYPE_CASE(tag, type)			\
      case GI_TYPE_TAG_ ## tag:			\
	size = sizeof (type);			\
	break;

      TYPE_CASE(BOOLEAN, gboolean);
      TYPE_CASE(INT8, gint8);
      TYPE_CASE(UINT8, guint8);
      TYPE_CASE(INT16, gint16);
      TYPE_CASE(UINT16, guint16);
      TYPE_CASE(INT32, gint32);
      TYPE_CASE(UINT32, guint32);
      TYPE_CASE(INT64, gint64);
      TYPE_CASE(UINT64, guint64);
      TYPE_CASE(FLOAT, gfloat);
      TYPE_CASE(DOUBLE, gdouble);
      TYPE_CASE(SHORT, gshort);
      TYPE_CASE(USHORT, gushort);
      TYPE_CASE(INT, gint);
      TYPE_CASE(UINT, guint);
      TYPE_CASE(LONG, glong);
      TYPE_CASE(ULONG, gulong);
      TYPE_CASE(SSIZE, gssize);
      TYPE_CASE(SIZE, gsize);
      TYPE_CASE(GTYPE, GType);
      TYPE_CASE(UTF8, gpointer);

#undef TYPE_CASE
    default:
      size = 0;
    }

  return size;
}

static int
lgi_simple_val_to_lua(lua_State* L, GITypeTag tag, GITransfer transfer,
		      GArgument* val)
{
  int vals = 1;
  switch (tag)
    {
      /* Simple (native) types. */
#define TYPE_CASE(tag, type, member, push, free)	\
      case GI_TYPE_TAG_ ## tag:				\
	push(L, val->member);				\
	if (transfer != GI_TRANSFER_NOTHING)		\
	  free;						\
	break

      TYPE_CASE(BOOLEAN, gboolean, v_boolean, lua_pushboolean, (void)0);
      TYPE_CASE(INT8, gint8, v_int8, lua_pushinteger, (void)0);
      TYPE_CASE(UINT8, guint8, v_uint8, lua_pushinteger, (void)0);
      TYPE_CASE(INT16, gint16, v_int16, lua_pushinteger, (void)0);
      TYPE_CASE(UINT16, guint16, v_uint16, lua_pushinteger, (void)0);
      TYPE_CASE(INT32, gint32, v_int32, lua_pushinteger, (void)0);
      TYPE_CASE(UINT32, guint32, v_uint32, lua_pushinteger, (void)0);
      TYPE_CASE(INT64, gint64, v_int64, lua_pushnumber, (void)0);
      TYPE_CASE(UINT64, guint64, v_uint64, lua_pushnumber, (void)0);
      TYPE_CASE(FLOAT, gfloat, v_float, lua_pushinteger, (void)0);
      TYPE_CASE(DOUBLE, gdouble, v_double, lua_pushinteger, (void)0);
      TYPE_CASE(SHORT, gshort, v_short, lua_pushinteger, (void)0);
      TYPE_CASE(USHORT, gushort, v_ushort, lua_pushinteger, (void)0);
      TYPE_CASE(INT, gint, v_int, lua_pushinteger, (void)0);
      TYPE_CASE(UINT, guint, v_uint, lua_pushinteger, (void)0);
      TYPE_CASE(LONG, glong, v_long, lua_pushinteger, (void)0);
      TYPE_CASE(ULONG, gulong, v_ulong, lua_pushinteger, (void)0);
      TYPE_CASE(SSIZE, gssize, v_ssize, lua_pushinteger, (void)0);
      TYPE_CASE(SIZE, gsize, v_size, lua_pushinteger, (void)0);
      TYPE_CASE(GTYPE, GType, v_long, lua_pushinteger, (void)0);
      TYPE_CASE(UTF8, gpointer, v_pointer, lua_pushstring,
		g_free(val->v_pointer));

#undef TYPE_CASE
    default:
      vals = 0;
    }

  return vals;
}

static int lgi_val_to_lua(lua_State* L, GITypeInfo* ti, GITransfer transfer,
			  GArgument* val);

static int
lgi_array_to_lua(lua_State* L, GITypeInfo* ti, GITransfer transfer,
		 GArgument* val)
{
  /* Find out the array length and element size. TODO: Handle 'length'
     variant.*/
  gint index, len = g_type_info_get_array_fixed_size(ti);
  GIArrayType atype = g_type_info_get_array_type(ti);
  GITypeInfo* eti = g_type_info_get_param_type(ti, 0);
  GITypeTag etag = g_type_info_get_tag(eti);
  gsize size = lgi_type_get_size(etag);
  gboolean zero_terminated = g_type_info_is_zero_terminated(ti);
  if (atype == GI_ARRAY_TYPE_ARRAY)
    len = ((GArray*)val->v_pointer)->len;

  if (val->v_pointer == NULL)
    /* NULL array is represented by nil. */
    lua_pushnil(L);
  else
    {
      /* Transfer type used for elements. */
      GITransfer realTransfer = (transfer == GI_TRANSFER_EVERYTHING) ?
	GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING;

      /* Create Lua table which will hold the array. */
      lua_createtable(L, len > 0 ? len : 0, 0);

      /* Iterate through array elements. */
      for (index = 0; len < 0 || index < len; index++)
	{
	  /* Get value from specified index. */
	  GArgument* eval;
	  gint offset = index * size;
	  if (atype == GI_ARRAY_TYPE_C)
	    eval = (GArgument*)((gchar*)val->v_pointer + offset);
	  else if (atype == GI_ARRAY_TYPE_ARRAY)
	    eval = (GArgument*)(((GArray*)val->v_pointer)->data + offset);

	  /* If the array is zero-terminated, terminate now and don't
	     include NULL entry. */
	  if (zero_terminated && eval->v_pointer == NULL)
	    break;

	  /* Store value into the table. */
	  if (lgi_val_to_lua(L, eti, realTransfer, eval) == 1)
	    lua_rawseti(L, -2, index + 1);
	}

      /* If needed, free the array itself. */
      if (transfer != GI_TRANSFER_NOTHING)
	{
	  if (atype == GI_ARRAY_TYPE_C)
	    g_free(val->v_pointer);
	  else if (atype == GI_ARRAY_TYPE_ARRAY)
	    g_array_unref((GArray*)val->v_pointer);
	}
    }

  g_base_info_unref(eti);
  return 1;
}

static int
lgi_val_to_lua(lua_State* L, GITypeInfo* ti, GITransfer transfer,
	       GArgument* val)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  int vals = lgi_simple_val_to_lua(L, tag, transfer, val);
  if (vals == 0)
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
	      case GI_INFO_TYPE_FLAGS:
		/* Resolve enum to the real value. */
		vals = lgi_simple_val_to_lua(L,
					     g_enum_info_get_storage_type(ii),
					     GI_TRANSFER_NOTHING, val);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_OBJECT:
		/* Create/Get compound object. */
		vals = compound_new(L, ii, &val->v_pointer, transfer);
		break;

	      default:
		vals = 0;
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	case GI_TYPE_TAG_ARRAY:
	  vals = lgi_array_to_lua(L, ti, transfer, val);
	  break;

	default:
	  vals = 0;
	}
    }

  return vals;
}

static int
lgi_simple_val_from_lua(lua_State* L, int index, GITypeTag tag,
			GArgument* val, gboolean optional)
{
  int vals = 1;
  switch (tag)
    {
#define TYPE_CASE(tag, type, member, expr)			\
      case GI_TYPE_TAG_ ## tag :				\
	val->member = (type)((optional &&			\
			      lua_isnoneornil(L, index)) ?	\
			     0 : expr);				\
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

#undef TYPE_CASE

    default:
      vals = 0;
    }

  return vals;
}

static int
lgi_val_from_lua(lua_State* L, int index, GITypeInfo* ti, GArgument* val,
		 gboolean optional)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  int vals = lgi_simple_val_from_lua(L, index, tag, val, optional);
  if (vals == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_INTERFACE:
	  /* Interface types.  Get the interface type and switch according to
	     the real type. */
	  {
	    GIBaseInfo* ii = g_type_info_get_interface(ti);
	    switch (g_base_info_get_type(ii))
	      {
	      case GI_INFO_TYPE_ENUM:
	      case GI_INFO_TYPE_FLAGS:
		/* Resolve Lua-number to enum value. */
		vals =
		  lgi_simple_val_from_lua(L, index,
					  g_enum_info_get_storage_type(ii),
					  val, optional);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_OBJECT:
		val->v_pointer = compound_get(L, index, ii, optional);
		vals = 1;
		break;

	      default:
		break;
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	default:
	  break;
	}
    }

  return vals;
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
    case GI_INFO_TYPE_OBJECT:
      vals = compound_new(L, ii, &val->v_pointer, GI_TRANSFER_CONTAINER);
      break;

    case GI_INFO_TYPE_CONSTANT:
      {
	GITypeInfo* ti = g_constant_info_get_type(ii);
	GArgument val;
	g_constant_info_get_value(ii, &val);
	vals = lgi_val_to_lua(L, ti, GI_TRANSFER_NOTHING, &val);
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

/* Loads reg_cache and ref_repo elements for compound arg on the stack.	 */
static struct ud_compound*
compound_load(lua_State* L, int arg)
{
  struct ud_compound* compound = luaL_checkudata(L, arg, UD_COMPOUND);
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_CACHE);
  lua_replace(L, -2);
  lua_rawgeti(L, -1, compound->ref_repo);
  return compound;
}

/* Calls repo metamethod (if exists) for compound at self, assumes
   that stack is loaded using compound_load.  Returns TRUE if the
   function was called. */
static gboolean
compound_callmeta(lua_State* L, const char* metaname, int nargs, int nrets)
{
  gboolean called = FALSE;

  /* Find method and check its type. */
  lua_rawgeti(L, nargs - 1, 0);
  lua_getfield(L, -1, metaname);
  lua_replace(L, -2);
  if (!lua_isnil(L, -1))
    {
      /* Perform the call, insert function before arguments. */
      lua_insert(L, - nargs - 1);
      lua_call(L, nargs, nrets);
      called = TRUE;
    }
  else
    {
      /* Cleanup the stack, as if the call would happen. */
      lua_pop(L, nargs + 1);
      if (nrets != 0)
	lua_settop(L, lua_gettop(L) + nrets);
    }

  return called;
}

static int
compound_new(lua_State* L, GIBaseInfo* info, gpointer* addr,
	     GITransfer transfer)
{
  int vals;
  struct ud_compound* compound;
  g_assert(addr != NULL);

  /* NULL pointer results in 'nil' compound. */
  if (transfer != GI_TRANSFER_CONTAINER && *addr == NULL)
    {
      lua_pushnil(L);
      vals = 1;
    }
  /* Check, whether struct is already in the cache. */
  else
    vals = lgi_get_cached(L, *addr);

  if (vals != 0)
    return vals;

  /* Find out how big data should be allocated. */
  size_t size = G_STRUCT_OFFSET(struct ud_compound, data);
  if (transfer == GI_TRANSFER_CONTAINER)
    size += g_struct_info_get_size(info);

  /* Create and initialize new userdata instance. */
  compound = lua_newuserdata(L, size);
  luaL_getmetatable(L, UD_COMPOUND);
  lua_setmetatable(L, -2);

  /* Load and remember reference to repo object. */
  compound->ref_repo = LUA_REFNIL;
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_REPO);
  lua_getfield(L, -1, g_base_info_get_namespace(info));
  if (!lua_isnil(L, -1))
    {
      lua_getfield(L, -1, g_base_info_get_name(info));
      compound->ref_repo = luaL_ref(L, -4);
    }
  lua_pop(L, 2);

  if (transfer == GI_TRANSFER_CONTAINER)
    *addr = compound->data;
  else if (transfer == GI_TRANSFER_NOTHING)
    {
      /* Try to acquire ownership if possible, because we are not sure
	 how long the object will be alive. */
      switch (g_base_info_get_type(info))
	{
	case GI_INFO_TYPE_OBJECT:
	  /* This is simple, ref the object. */
	  g_object_ref(*addr);
	  transfer = GI_TRANSFER_EVERYTHING;
	  break;

	case GI_INFO_TYPE_STRUCT:
	  /* If the metatable contains 'acquire', call it. */
	  lua_pushvalue(L, -2);
	  if (compound_callmeta(L, "acquire", 1, 0))
	    transfer = GI_TRANSFER_EVERYTHING;
	  break;

	default:
	  break;
	}
    }

  compound->addr = *addr;
  compound->owns = (transfer == GI_TRANSFER_EVERYTHING);
  lua_pop(L, 1);

  /* Store newly created compound to the cache. */
  lgi_set_cached(L, compound);
  return 1;
};

static int
compound_gc(lua_State* L)
{
  struct ud_compound* compound = compound_load(L, 1);
  if (compound->owns && !lua_isnil(L, -1))
    {
      GIInfoType type;

      /* Check the type of the compound. */
      lua_rawgeti(L, -1, 0);
      lua_getfield(L, -1, "type");
      type = lua_tointeger(L, -1);
      lua_pop(L, 2);
      switch (type)
	{
	case GI_INFO_TYPE_STRUCT:
	  /* Call dispose method, if available. */
	  lua_pushvalue(L, 1);
	  compound_callmeta(L, "dispose", 1, 0);
	  break;

	case GI_INFO_TYPE_OBJECT:
	  /* Simply unref the object. */
	  g_object_unref(compound->addr);
	  break;

	default:
	  break;
	}
    }

  /* Free the reference to the repo in cache regtable. */
  luaL_unref(L, -2, compound->ref_repo);
  return 0;
}

static int
compound_tostring(lua_State* L)
{
  struct ud_compound* compound = compound_load(L, 1);
  lua_pushfstring(L, "lgi %p:", compound);
  if (!lua_isnil(L, -2))
    {
      lua_rawgeti(L, -2, 0);
      lua_getfield(L, -2, "name");
      lua_insert(L, -3);
      lua_pop(L, 1);
      lua_concat(L, 2);
    }
  return 1;
}

/* Reports error related to given compound element. Expects
   compound_loaded' stack layout.*/
static int
compound_error(lua_State* L, const char* errmsg, int element)
{
  /* Prepare name of the compound. */
  lua_rawgeti(L, -1, 0);
  lua_getfield(L, -1, "name");
  return luaL_error(L, errmsg, lua_tostring(L, -1), lua_tostring(L, element));
}

static gpointer
compound_get(lua_State* L, int index, GIBaseInfo* ii, gboolean optional)
{
  struct ud_compound* compound;
  GType type;
  if (optional)
    {
      compound = lua_touserdata(L, index);
      if (compound == NULL)
	return NULL;

      if (!lua_getmetatable(L, index))
	return NULL;

      lua_getfield(L, LUA_REGISTRYINDEX, UD_COMPOUND);
      if (!lua_rawequal(L, -1, -2))
	compound = NULL;
      lua_pop(L, 2);
      if (compound == NULL)
	return NULL;
    }
  else
    compound = luaL_checkudata(L, index, UD_COMPOUND);

  /* Final check for type ancestry.  Faster one is using dynamic
     introspection from g_type, but not all 'ii' have that. */
  type = g_registered_type_info_get_g_type(ii);
  if (G_TYPE_IS_DERIVED(type))
    {
      /* Compare using G_TYPE machinery. */
      GType real = G_TYPE_FROM_INSTANCE(compound->addr);
      if (!g_type_is_a(real, type))
	{
	  if (!optional)
	    luaL_argerror(L, index, g_type_name(real));
	  else
	    return NULL;
	}
    }
  else
    {
      /* Compare strings using repo and GI machinery. */
      lua_pushstring(L, g_base_info_get_namespace(ii));
      lua_pushstring(L, ".");
      lua_pushstring(L, g_base_info_get_name(ii));
      lua_concat(L, 3);
      lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
      lua_rawgeti(L, -1, LGI_REG_REPO);
      lua_rawgeti(L, -1, compound->ref_repo);
      if (!lua_isnil(L, -1))
	{
	  lua_rawgeti(L, -1, 0);
	  lua_getfield(L, -1, "name");
	  if (g_strcmp0(lua_tostring(L, -1), lua_tostring(L, -6)) != 0)
	    {
	      if (!optional)
		luaL_argerror(L, index, lua_tostring(L, -1));
	      else
		compound = NULL;
	    }

	  lua_pop(L, 2);
	}

      lua_pop(L, 4);
    }

  return compound != NULL ? compound->addr : NULL;
}

/* Processes compound element of 'field' type. */
static int
compound_element_field(lua_State* L, gpointer addr, GIFieldInfo* fi, int newval)
{
  GArgument* val = G_STRUCT_MEMBER_P(addr, g_field_info_get_offset(fi));
  GITypeInfo* ti = g_field_info_get_type(fi);
  int vals;
  if (newval == -1)
    vals = lgi_val_to_lua(L, ti, GI_TRANSFER_NOTHING, val);
  else
    vals = lgi_val_from_lua(L, newval, ti, val, FALSE);

  g_base_info_unref(ti);
  return vals;
}

/* Calls compound_load (arg1), checks elemenet (arg2), and processes
   it; either reads it to stack (newval = -1) or sets it to value at
   newval stack. */
static int
compound_element(lua_State* L, int newval)
{
  /* Load compound and element. */
  int vals = 0, type;
  struct ud_compound* compound = compound_load(L, 1);
  lua_pushvalue(L, 2);
  lua_gettable(L, -2);
  type = lua_type(L, -1);
  if (type == LUA_TNIL)
    {
      /* Not found.  Try calling meta index/newindex. */
      lua_pop(L, 1);
      lua_pushvalue(L, 1);
      lua_pushvalue(L, 2);
      if (newval != -1)
	lua_pushvalue(L, newval);
      else
	vals = 1;
      if (!compound_callmeta(L, newval == -1 ? "index" : "newindex",
			     3 - vals, vals))
	return compound_error(L, "%s: no `%s'", 2);
    }
  else
    {
      /* Special handling is for compound-userdata, which contain some
	 kind of baseinfo. */
      GIBaseInfo* ei = compound_get(L, -1, lgi_baseinfo_info, TRUE);
      if (ei != NULL)
	{
	  switch (g_base_info_get_type(ei))
	    {
	    case GI_INFO_TYPE_FIELD:
	      vals = compound_element_field(L, compound->addr, ei, newval);
	      break;

	    case GI_INFO_TYPE_PROPERTY:
	      break;

	    default:
	      break;
	    }

	  /* Don't unref ei, its lifetime is controlled by userdata. */
	}
      else
	{
	  /* Everything else is simply forwarded for index, or error
	     for newindex. */
	  if (newval != -1)
	    {
	      lua_pop(L, 1);
	      return compound_error(L, "%s: `%s' not writable", 2);
	    }
	  else
	    vals = 1;
	}
    }

  return vals;
}

static int
compound_index(lua_State* L)
{
  return compound_element(L, -1);
}

static int
compound_newindex(lua_State* L)
{
  return compound_element(L, 3);
}

static const struct luaL_reg struct_reg[] = {
  { "__gc", compound_gc },
  { "__tostring", compound_tostring },
  { "__index", compound_index },
  { "__newindex", compound_newindex },
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
  gint i, argc, argffi, flags, lua_argi, ti_argi, ffi_argi;
  gboolean has_self, throws;
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
  g_printf("call: %s\n", g_base_info_get_name(function->info));
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
      /* 'self' handling: check for object type and marshall it in
	 from lua. */
      GIBaseInfo* pi = g_base_info_get_container(function->info);
      args[1].arg.v_pointer = compound_get(L, lua_argi, pi, TRUE);

      /* Advance to the next argument. */
      lua_argi++;
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
  ffi_argi = has_self ? 2 : 1;
  ti_argi = 0;

  /* Handle return value. */
  g_callable_info_load_return_type(function->info, &args[0].ti);
  lua_argi += lgi_val_to_lua(L, &args[0].ti,
			     g_callable_info_get_caller_owns(function->info),
			     &args[0].arg);

  /* Handle parameters. */
  for (i = 0; i < argc; i++, ffi_argi++)
    {
      if (args[ffi_argi].dir == GI_DIRECTION_OUT ||
	  args[ffi_argi].dir == GI_DIRECTION_INOUT)
	lua_argi +=
	  lgi_val_to_lua(L, &args[ffi_argi].ti,
			 g_arg_info_get_ownership_transfer(&args[ffi_argi].ai),
			 &args[ffi_argi].arg);
    }

  return lua_argi;
}

static const struct luaL_reg function_reg[] = {
  { "__gc", function_gc },
  { "__call", function_call },
  { "__tostring", function_tostring },
  { NULL, NULL }
};

static int
lgi_find(lua_State* L)
{
  const gchar* symbol = luaL_checkstring(L, 1);
  const gchar* container = luaL_optstring(L, 2, NULL);
  GIBaseInfo *info, *fi;
  int vals = 0;

  g_printf("find: %s.%s\n", symbol, container);

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
  vals = compound_new(L, lgi_baseinfo_info, (gpointer*)&info,
		      GI_TRANSFER_EVERYTHING);
  return vals;
}

static int
lgi_get(lua_State* L)
{
  /* Create new instance based on the embedded typeinfo. */
  GArgument unused;
  return lgi_type_new(L, compound_get(L, 1, lgi_baseinfo_info, FALSE), &unused);
}

static const struct luaL_reg lgi_reg[] = {
  { "find", lgi_find },
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

int
luaopen_lgi__core(lua_State* L)
{
  GError* err = NULL;

  /* GLib initializations. */
  g_type_init();
  g_irepository_require(NULL, "GIRepository", NULL, 0, &err);
  if (err != NULL)
    lgi_throw(L, err);
  lgi_baseinfo_info = g_irepository_find_by_name(NULL, "GIRepository",
						 "IBaseInfo");

  /* Register userdata types. */
  lgi_reg_udata(L, struct_reg, UD_COMPOUND);
  lgi_reg_udata(L, function_reg, UD_FUNCTION);

  /* Register _core interface. */
  luaL_register(L, "lgi._core", lgi_reg);

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
  lgi_create_reg(L, LGI_REG_CACHE, NULL, TRUE);

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
