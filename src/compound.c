/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 * License: MIT.
 *
 * Management of compounds, i.e. structs, unions, objects interfaces, wrapped
 * into single Lua userdata blcok called 'compound'.
 */

#include "lgi.h"

/* Creates new userdata representing instance of struct/object
   described by 'ii'.  Transfer describes, whether the
   ownership is transferred and gc method releases the object.	The
   special transfer value is GI_TRANSFER_CONTAINER, which means that
   the structure is allocated and its address is put into addr
   (i.e. addr parameter is output in this case). */
static int compound_store(lua_State* L, GIBaseInfo* ii, gpointer* addr,
                          GITransfer transfer);

/* Retrieves compound-type parameter from given Lua-stack position, checks,
   whether it is suitable for requested ii type.  Returns pointer to the
   compound object, returns NULL if Lua-stack value is nil and optional is
   TRUE. */
static gpointer compound_load(lua_State* L, int arg, GIBaseInfo* ii,
                              gboolean optional);

/* 'compound' userdata: wraps compound with reference to its repo table. */
typedef struct _Compound
{
  /* Address of the structure data. */
  gpointer addr;

  /* Lua reference to repo table representing this compound. */
  int ref_repo : 31;

  /* Flag indicating whether compound is owned. */
  int owns : 1;

  /* If the structure is allocated 'on the stack', its data is here. */
  gchar data[1];
} Compound;

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
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)              \
      case tag:							\
	size = sizeof(ctype);					\
	break;
#include "decltype.h"

    default:
      size = 0;
    }

  return size;
}

static int
lgi_simple_val_to_lua(lua_State* L, GITypeTag tag, GITransfer transfer,
		      GIArgument* val)
{
  int vals = 1;
  switch (tag)
    {
      /* Simple (native) types. */
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)              \
      case tag:							\
	push(L, val->argf);					\
	if (transfer != GI_TRANSFER_NOTHING)			\
	  dtor(val->argf);					\
	break;
#include "decltype.h"

    default:
      vals = 0;
    }

  return vals;
}

static int lgi_val_to_lua(lua_State* L, GITypeInfo* ti, GITransfer transfer,
			  GIArgument* val);

static int
lgi_array_to_lua(lua_State* L, GITypeInfo* ti, GITransfer transfer,
		 GIArgument* val)
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
	  GIArgument* eval;
	  gint offset = index * size;
	  if (atype == GI_ARRAY_TYPE_C)
	    eval = (GIArgument*)((gchar*)val->v_pointer + offset);
	  else if (atype == GI_ARRAY_TYPE_ARRAY)
	    eval = (GIArgument*)(((GArray*)val->v_pointer)->data + offset);

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
	       GIArgument* val)
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
		vals = compound_store(L, ii, &val->v_pointer, transfer);
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
			GIArgument* val, gboolean optional)
{
  int vals = 1;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)              \
      case tag :						\
	val->argf = (ctype)((optional &&			\
			      lua_isnoneornil(L, index)) ?	\
			    0 : check(L, index));		\
	break;
#include "decltype.h"

    default:
      vals = 0;
    }

  return vals;
}

static int
lgi_val_from_lua(lua_State* L, int index, GITypeInfo* ti, GIArgument* val,
		 gboolean optional)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  int vals = lgi_simple_val_from_lua(L, index, tag, val, optional);
  if (vals == 0 && tag == GI_TYPE_TAG_INTERFACE)
    {
      /* Interface types.  Get the interface type and switch according to
	 the real type. */
      GIBaseInfo* ii = g_type_info_get_interface(ti);
      switch (g_base_info_get_type(ii))
	{
	case GI_INFO_TYPE_ENUM:
	case GI_INFO_TYPE_FLAGS:
	  /* Resolve Lua-number to enum value. */
	  vals = lgi_simple_val_from_lua(L, index,
					 g_enum_info_get_storage_type(ii),
					 val, optional);
	  break;

	case GI_INFO_TYPE_STRUCT:
	case GI_INFO_TYPE_OBJECT:
	  val->v_pointer = compound_load(L, index, ii, optional);
	  vals = 1;
	  break;

	default:
	  break;
	}
      g_base_info_unref(ii);
    }

  return vals;
}

/* Retrieves gtype for specified baseinfo. */
static GType
repo_get_gtype(lua_State* L, GIBaseInfo* ii)
{
  GType type;
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_REPO);
  lua_getfield(L, -1, g_base_info_get_namespace(ii));
  lua_getfield(L, -1, g_base_info_get_name(ii));
  if (lua_isnil(L, -1))
    luaL_error(L, "`%s.%s' not present in repo", g_base_info_get_namespace(ii),
	       g_base_info_get_name(ii));
  lua_rawgeti(L, -1, 0);
  lua_getfield(L, -1, "gtype");
  type = (GType)luaL_checkinteger(L, -1);
  lua_pop(L, 6);
  return type;
}

/* Initializes type of GValue to specified ti. */
static void
value_init(lua_State* L, GValue* val, GITypeInfo* ti)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 val_type, val_get, val_set, ffitype)           \
      case tag:							\
	g_value_init(val, val_type);				\
	break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface(ti);
	GIInfoType type = g_base_info_get_type(ii);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	  case GI_INFO_TYPE_OBJECT:
	    g_value_init(val, repo_get_gtype(L, ii));
	    break;

	  default:
	    g_base_info_unref(ii);
	    luaL_error(L, "value_init: bad ti.iface.type=%d", (int)type);
	  }
	g_base_info_unref(ii);
      }
      break;

    default:
      luaL_error(L, "value_init: bad ti.tag=%d", (int)tag);
    }
}

/* Loads GValue contents from specified stack position, expects ii type.
   Assumes that val is already inited by value_init(). */
static int
value_load(lua_State* L, GValue* val, int narg, GITypeInfo* ti)
{
  int vals = 1;
  switch (g_type_info_get_tag(ti))
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 val_type, val_get, val_set, ffitype)           \
      case tag:							\
	val_set(val, check(L, narg));				\
	break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface(ti);
	switch (g_base_info_get_type(ii))
	  {
	  case GI_INFO_TYPE_ENUM:
	    g_value_set_enum(val, luaL_checkinteger(L, narg));
	    break;

	  case GI_INFO_TYPE_FLAGS:
	    g_value_set_flags(val, luaL_checkinteger(L, narg));
	    break;

	  case GI_INFO_TYPE_OBJECT:
	    g_value_set_object(val, compound_load(L, narg, ii, FALSE));
	    break;

	  case GI_INFO_TYPE_STRUCT:
	    return luaL_error(L, "don't know how to handle struct->GValue");

	  default:
	    vals = 0;
	  }
	g_base_info_unref(ii);
      }
      break;

    default:
      vals = 0;
    }

  return vals;
}

/* Pushes GValue content to stack, assumes that value is of ii type. */
static int
value_store(lua_State* L, GValue* val, GITypeInfo* ti)
{
  int vals = 1;
  switch (g_type_info_get_tag(ti))
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 val_type, val_get, val_set, ffitype)           \
      case tag:							\
	push(L, val_get(val));					\
	break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface(ti);
	switch (g_base_info_get_type(ii))
	  {
	  case GI_INFO_TYPE_ENUM:
	    lua_pushinteger(L, g_value_get_enum(val));
	    break;

	  case GI_INFO_TYPE_FLAGS:
	    lua_pushinteger(L, g_value_get_flags(val));
	    break;

	  case GI_INFO_TYPE_OBJECT:
	    {
	      gpointer addr = g_value_dup_object(val);
	      vals = compound_store(L, ii, &addr, GI_TRANSFER_EVERYTHING);
	    }
	    break;

	  case GI_INFO_TYPE_STRUCT:
	    return luaL_error(L, "don't know how to handle GValue->struct");

	  default:
	    vals = 0;
	  }
	g_base_info_unref(ii);
      }
      break;

    default:
      vals = 0;
    }

  return vals;
}

/* Loads reg_typeinfo and ref_repo elements for compound arg on the stack.  */
static Compound*
compound_prepare(lua_State* L, int arg)
{
  Compound* compound = luaL_checkudata(L, arg, LGI_COMPOUND);
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_TYPEINFO);
  lua_replace(L, -2);
  lua_rawgeti(L, -1, compound->ref_repo);
  g_assert(!lua_isnil(L, -1));
  return compound;
}

/* Calls repo metamethod (if exists) for compound at self, assumes
   that ref_repo is on the top of the stack.  Returns TRUE if the
   function was called. */
static gboolean
compound_callmeta(lua_State* L, const char* metaname, int nargs, int nrets)
{
  gboolean called = FALSE;

  /* Find method and check its type. */
  lua_rawgeti(L, - nargs - 1, 0);
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

gboolean
lgi_compound_create(lua_State* L, GIBaseInfo* ii, gpointer addr,
		    gboolean own)
{
  return compound_store(L, ii, &addr, 
			own ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
}

int
lgi_compound_create_struct(lua_State* L, GIBaseInfo* ii, gpointer* addr)
{
    return compound_store(L, ii, addr, GI_TRANSFER_CONTAINER);
}

static int
compound_store(lua_State* L, GIBaseInfo* info, gpointer* addr,
              GITransfer transfer)
{
  int vals;
  Compound* compound;
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
  size_t size = G_STRUCT_OFFSET(Compound, data);
  if (transfer == GI_TRANSFER_CONTAINER)
    size += g_struct_info_get_size(info);

  /* Create and initialize new userdata instance. */
  compound = lua_newuserdata(L, size);
  luaL_getmetatable(L, LGI_COMPOUND);
  lua_setmetatable(L, -2);

  /* Load ref_repo reference to repo table of the object. */
  compound->ref_repo = LUA_REFNIL;
  lua_rawgeti(L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti(L, -1, LGI_REG_REPO);
  lua_getfield(L, -1, g_base_info_get_namespace(info));
  lua_getfield(L, -1, g_base_info_get_name(info));
  lua_replace(L, -3);
  lua_pop(L, 1);

  /* Store it to the typeinfo. */
  lua_rawgeti(L, -2, LGI_REG_TYPEINFO);
  lua_pushvalue(L, -2);
  compound->ref_repo = luaL_ref(L, -2);

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
  lua_pop(L, 3);

  /* Store newly created compound to the cache. */
  lgi_set_cached(L, compound);
  return 1;
};

static int
compound_gc(lua_State* L)
{
  Compound* compound = compound_prepare(L, 1);
  if (compound->owns)
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
        case GI_INFO_TYPE_INTERFACE:
	  /* Simply unref the object. */
	  g_object_unref(compound->addr);
	  break;

	default:
          g_warning("Incorrect type %d in compound_gc(%p)", (int)type,
                    compound);
	  break;
	}
    }

  /* Free the reference to the repo object in typeinfo regtable. */
  luaL_unref(L, -2, compound->ref_repo);
  return 0;
}

static int
compound_tostring(lua_State* L)
{
  Compound* compound = compound_prepare(L, 1);
  lua_pushfstring(L, "lgi %p:", compound);
  lua_rawgeti(L, -2, 0);
  lua_getfield(L, -1, "name");
  lua_replace(L, -2);
  lua_concat(L, 2);
  return 1;
}

/* Reports error related to given compound element. Expects
   compound_prepared' stack layout.*/
static int
compound_error(lua_State* L, const char* errmsg, int element)
{
  /* Prepare name of the compound. */
  lua_rawgeti(L, -2, 0);
  lua_getfield(L, -1, "name");
  return luaL_error(L, errmsg, lua_tostring(L, -1), lua_tostring(L, element));
}

gpointer
lgi_compound_get(lua_State* L, int arg, GIBaseInfo* ii, gboolean optional)
{
  return compound_load(L, arg, ii, optional);
}

static gpointer
compound_load(lua_State* L, int index, GIBaseInfo* ii, gboolean optional)
{
  Compound* compound;
  GType type;
  if (optional)
    {
      compound = lua_touserdata(L, index);
      if (compound == NULL)
	return NULL;

      if (!lua_getmetatable(L, index))
	return NULL;

      lua_getfield(L, LUA_REGISTRYINDEX, LGI_COMPOUND);
      if (!lua_rawequal(L, -1, -2))
	compound = NULL;
      lua_pop(L, 2);
      if (compound == NULL)
	return NULL;
    }
  else
    compound = luaL_checkudata(L, index, LGI_COMPOUND);

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
      compound = compound_prepare(L, index);
      lua_rawgeti(L, -1, 0);
      lua_getfield(L, -1, "name");
      lua_pushstring(L, g_base_info_get_namespace(ii));
      lua_pushstring(L, ".");
      lua_pushstring(L, g_base_info_get_name(ii));
      lua_concat(L, 3);
      if (g_strcmp0(lua_tostring(L, -1), lua_tostring(L, -2)) != 0)
      {
	  if (!optional)
	      luaL_argerror(L, index, lua_tostring(L, -1));
	  else
	      compound = NULL;
      }
      lua_pop(L, 5);
    }

  return compound != NULL ? compound->addr : NULL;
}

/* Processes compound element of 'field' type. */
static int
compound_element_field(lua_State* L, gpointer addr, GIFieldInfo* fi, int newval)
{
  GIArgument* val = G_STRUCT_MEMBER_P(addr, g_field_info_get_offset(fi));
  GITypeInfo* ti = g_field_info_get_type(fi);
  int flags = g_field_info_get_flags(fi);
  int vals;
  if (newval == -1)
    {
      if ((flags & GI_FIELD_IS_READABLE) == 0)
	{
	  g_base_info_unref(ti);
	  return luaL_argerror(L, 2, "not readable");
	}

      vals = lgi_val_to_lua(L, ti, GI_TRANSFER_NOTHING, val);
    }
  else
    {
      if ((flags & GI_FIELD_IS_WRITABLE) == 0)
	{
	  g_base_info_unref(ti);
	  return luaL_argerror(L, 2, "not writable");
	}

      vals = lgi_val_from_lua(L, newval, ti, val, FALSE);
    }

  g_base_info_unref(ti);
  return vals;
}

/* Processes compound element of 'property' type. */
static int
compound_element_property(lua_State* L, gpointer addr, GIPropertyInfo* pi,
			  int newval)
{
  int vals = 0, flags = g_property_info_get_flags(pi);
  GITypeInfo* ti = g_property_info_get_type(pi);
  const gchar* name = g_base_info_get_name(pi);
  GValue val = {0};

  value_init(L, &val, ti);

  if (newval == -1)
    {
      if ((flags & G_PARAM_READABLE) == 0)
	{
	  g_base_info_unref(ti);
	  return luaL_argerror(L, 2, "not readable");
	}

      g_object_get_property((GObject*)addr, name, &val);
      vals = value_store(L, &val, ti);
    }
  else
    {
      if ((flags & G_PARAM_WRITABLE) == 0)
	{
	  g_base_info_unref(ti);
	  return luaL_argerror(L, 2, "not writable");
	}

      vals = value_load(L, &val, 3, ti);
      g_object_set_property((GObject*)addr, name, &val);
    }

  g_value_unset(&val);
  return vals;
}

/* Calls compound_prepare(arg1), checks element (arg2), and processes
   it; either reads it to stack (newval = -1) or sets it to value at
   newval stack. */
static int
compound_element(lua_State* L, int newval)
{
  /* Load compound and element. */
  int vals = 0, type;
  Compound* compound = compound_prepare(L, 1);
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
      GIBaseInfo* ei = compound_load(L, -1, lgi_baseinfo_info, TRUE);
      if (ei != NULL)
	{
	  switch (g_base_info_get_type(ei))
	    {
	    case GI_INFO_TYPE_FIELD:
	      vals = compound_element_field(L, compound->addr, ei, newval);
	      break;

	    case GI_INFO_TYPE_PROPERTY:
	      vals = compound_element_property(L, compound->addr, ei, newval);
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

const struct luaL_reg lgi_compound_reg[] = {
  { "__gc", compound_gc },
  { "__tostring", compound_tostring },
  { "__index", compound_index },
  { "__newindex", compound_newindex },
  { NULL, NULL }
};
