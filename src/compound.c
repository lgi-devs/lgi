/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 * License: MIT.
 *
 * Management of compounds, i.e. structs, unions, objects interfaces, wrapped
 * into single Lua userdata block called 'compound'.
 */

#include <string.h>
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

/* Initializes type of GValue to specified ti. */
static void
value_init (lua_State *L, GValue *val, GITypeInfo *ti)
{
  GITypeTag tag = g_type_info_get_tag (ti);
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup, \
                 val_type, val_get, val_set, ffitype)           \
    case tag:                                                   \
      g_value_init (val, val_type);                             \
      break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface (ti);
	GIInfoType type = g_base_info_get_type (ii);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_STRUCT:
            g_value_init (val, g_registered_type_info_get_g_type (ii));
	    break;

	  default:
	    g_base_info_unref (ii);
	    luaL_error (L, "value_init: bad ti.iface.type=%d", (int) type);
	  }
	g_base_info_unref (ii);
      }
      break;

    default:
      luaL_error (L, "value_init: bad ti.tag=%d", (int) tag);
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
  /* Avoid creating non-boxed structures, because we do not know how
     to destroy them. */
  if (!g_type_is_a (g_registered_type_info_get_g_type (ii), G_TYPE_BOXED))
    {
      lua_pushfstring (L, "unable to create `%s.%s': non-boxed struct",
		       g_base_info_get_namespace (ii),
		       g_base_info_get_name (ii));
      lua_error (L);
    }

  return compound_store (L, ii, addr, GI_TRANSFER_CONTAINER);
}

gboolean
lgi_compound_create_object (lua_State *L, GIObjectInfo *oi, int argtable,
			    gpointer *addr)
{
  gint n_params = 0;
  GParameter *params = NULL, *param;
  GIPropertyInfo *pi;
  GITypeInfo *ti;

  /* Check, whether 2nd argument is table containing construction-time
     properties. */
  if (!lua_isnoneornil (L, argtable))
    {
      luaL_checktype (L, argtable, LUA_TTABLE);

      /* Find out how many parameters we have. */
      lua_pushnil (L);
      for (lua_pushnil (L); lua_next (L, 2) != 0; lua_pop (L, 1))
	n_params++;

      if (n_params > 0)
	{
	  /* Allocate GParameter array (on the stack) and fill it
	     with parameters. */
	  param = params = g_newa (GParameter, n_params);
	  memset (params, 0, sizeof (GParameter) * n_params);
	  for (lua_pushnil (L); lua_next (L, 2) != 0; lua_pop (L, 1), param++)
	    {
	      /* Get property info. */
	      Compound *compound = lua_touserdata (L, -2);
	      if (G_UNLIKELY (compound == NULL ||
			      (pi = compound->addr) == NULL))
		{
		  lua_pushfstring (L, "bad ctor property for %s.%s",
				   g_base_info_get_namespace (oi),
				   g_base_info_get_name (oi));
		  luaL_argerror (L, argtable, lua_tostring (L, -1));
		}
	      else
		{
		  /* Extract property name. */
		  param->name = g_base_info_get_name (pi);
		  g_debug ("processing property %s", param->name);

		  /* Initialize and load parameter value from the table
		     contents. */
		  ti = g_property_info_get_type (pi);
		  value_init (L, &param->value, ti);
		  value_load (L, &param->value, -1, ti);
		  g_base_info_unref (ti);
		}
	    }

	  /* Pop the repo class table. */
	  lua_pop (L, 1);
	}
    }

  /* Create the object. */
  *addr = g_object_newv (g_registered_type_info_get_g_type (oi), n_params,
			 params);

  /* We do not want any floating references, so convert them if we
     have them. */
  if (g_object_is_floating (*addr))
    g_object_ref_sink (*addr);

  /* And wrap a nice userdata around it. */
  return compound_store (L, oi, addr, GI_TRANSFER_EVERYTHING);
}

static int
compound_store (lua_State* L, GIBaseInfo* info, gpointer* addr,
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
compound_gc (lua_State* L)
{
  Compound* compound = compound_prepare (L, 1);
  if (compound->owns)
    {
      /* Check the gtype of the compound. */
      GType gtype;
      lua_rawgeti (L, -1, 0);
      lua_getfield (L, -1, "gtype");
      gtype = lua_tointeger (L, -1);
      lua_pop (L, 2);

      /* Decide what to do according to the type. */
      if (G_TYPE_IS_OBJECT (gtype))
	g_object_unref (compound->addr);
      else if (G_TYPE_IS_BOXED (gtype))
	g_boxed_free (gtype, compound->addr);
    }

  /* Free the reference to the repo object in typeinfo regtable. */
  luaL_unref (L, -2, compound->ref_repo);
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
  GType requested_type, real_type;

  if (optional && lua_isnoneornil(L, index))
    return NULL;

  /* Check for type ancestry. */
  compound = compound_prepare(L, index);
  lua_rawgeti(L, -1, 0);
  lua_getfield(L, -1, "gtype");
  real_type = lua_tointeger(L, -1);
  requested_type = g_registered_type_info_get_g_type(ii);
  if (!g_type_is_a(real_type, requested_type))
    luaL_argerror(L, index, g_type_name(requested_type));

  lua_pop(L, 4);
  return compound->addr;
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
	  g_base_info_unref (ti);
	  return luaL_argerror (L, 2, "not readable");
	}

      vals = lgi_marshal_2lua (L, ti, val, GI_TRANSFER_NOTHING, NULL, NULL) ?
	1 : 0;
    }
  else
    {
      if ((flags & GI_FIELD_IS_WRITABLE) == 0)
	{
	  g_base_info_unref (ti);
	  return luaL_argerror (L, 2, "not writable");
	}

      lua_pop (L, lgi_marshal_2c (L, ti, NULL, GI_TRANSFER_NOTHING, val,
				  newval, NULL, NULL));
    }

  g_base_info_unref (ti);
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
      /* Not found. */
    return compound_error(L, "%s: no `%s'", 2);
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
