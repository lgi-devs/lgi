/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Management of structures and unions (i.e. records).
 */

#include <string.h>
#include "lgi.h"

/* Userdata containing record reference. Table with record type is
   attached as userdata environment. */
typedef struct _Record
{
  /* Address of the record memory data. */
  gpointer addr;

  /* Ownership mode of the record. */
  unsigned mode : 2;

  /* Flag indicating whether this record is union. */
  unsigned is_union : 1;

  union
  {
    /* If the record is allocated 'on the stack', its data is here. */
    gchar data[1];

    /* If the record is allocated inside parent record, a
       luaL_ref-type reference into LUA_REGISTRYINDEX of parent object
       is stored here. */
    int parent;
  } data;
} Record;

/* lightuserdata key to LUA_REGISTRYINDEX containing metatable for
   record. */
static int record_mt;

/* lightuserdata key to cache table containing
   lightuserdata(record->addr)->weak(record) */
static int record_cache;

gpointer
lgi_record_2lua (lua_State *L, GIBaseInfo *info, gpointer addr,
		 LgiRecordMode mode, int parent)
{
  size_t size;
  Record *record;
  gboolean is_union;

  /* Convert 'parent' index to an absolute one. */
  luaL_checkstack (L, 6, "");
  lgi_makeabs (L, parent);

  /* NULL pointer results in 'nil'. */
  if (mode != LGI_RECORD_ALLOCATE && addr == NULL)
    {
      lua_pushnil (L);
      return NULL;
    }

  /* Prepare access to registry and cache. */
  lua_pushlightuserdata (L, &record_cache);
  lua_rawget (L, LUA_REGISTRYINDEX);

  /* Check whether the record is already cached. */
  lua_pushlightuserdata (L, addr);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1) && mode != LGI_RECORD_PARENT)
    {
      /* Remove unneeded tables under our requested object. */
      lua_replace (L, -2);

      /* In case that we want to own the record, make sure that the
	 ownership is properly updated. */
      record = lua_touserdata (L, -1);
      g_assert (record->addr == addr);
      if (mode == LGI_RECORD_OWN && record->mode == LGI_RECORD_PEEK)
	record->mode = mode;

      return addr;
    }

  /* Calculate size of the record to allocate. */
  is_union = g_base_info_get_type (info) == GI_INFO_TYPE_UNION;
  if (mode == LGI_RECORD_ALLOCATE)
    size = G_STRUCT_OFFSET (Record, data) +  (is_union
					      ? g_union_info_get_size (info)
					      : g_struct_info_get_size (info));
  else
    size = (parent == 0) ? G_STRUCT_OFFSET (Record, data) : sizeof (Record);

  /* Allocate new userdata for record object, attach proper
     metatable. */
  record = lua_newuserdata (L, size);
  lua_pushlightuserdata (L, &record_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);
  if (mode == LGI_RECORD_ALLOCATE)
    {
      addr = record->data.data;
      memset (addr, 0, size - G_STRUCT_OFFSET (Record, data));
    }
  else if (mode == LGI_RECORD_PARENT)
    {
      /* Store reference to the parent argument. */
      lua_pushvalue (L, parent);
      record->data.parent = luaL_ref (L, LUA_REGISTRYINDEX);
    }
  record->addr = addr;
  record->mode = mode;
  record->is_union = is_union ? 1 : 0;

  /* Get ref_repo table according to the 'info'. */
  lua_pushlightuserdata (L, &lgi_addr_repo);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_getfield (L, -1, g_base_info_get_namespace (info));
  lua_getfield (L, -1, g_base_info_get_name (info));
  g_assert (!lua_isnil (L, -1));

  /* Attach found table as environment data for created Record. */
  lua_setfenv (L, -4);
  lua_pop (L, 2);

  /* Store newly created record into the cache. */
  if (mode != LGI_RECORD_PARENT)
    {
      lua_pushlightuserdata (L, addr);
      lua_pushvalue (L, -2);
      lua_rawset (L, -5);
    }

  /* Clean up the stack; remove cache table from under our result. */
  lua_replace (L, -3);
  lua_pop (L, 1);
  return addr;
}

/* Checks that given argument is Record userdata and returns pointer
   to it. Returns NULL if narg has bad type. */
static Record *
record_check (lua_State *L, int narg)
{
  /* Check using metatable that narg is really Record type. */
  Record *record = lua_touserdata (L, narg);
  luaL_checkstack (L, 3, "");
  if (!lua_getmetatable (L, narg))
    return NULL;
  lua_pushlightuserdata (L, &record_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  if (!lua_equal (L, -1, -2))
    record = NULL;
  lua_pop (L, 2);
  return record;
}

/* Throws error that narg is not of expected type. */
static int
record_error (lua_State *L, int narg, GIBaseInfo *ri)
{
  luaL_checkstack (L, 3, "");
  lua_pushstring (L, lua_typename (L, lua_type (L, narg)));
  if (ri)
    lua_concat (L, lgi_type_get_name (L, ri));
  else
    lua_pushliteral (L, "lgi.record");
  lua_pushfstring (L, "%s expected, got %s", lua_tostring (L, -1),
		   lua_tostring (L, -2));
  return luaL_argerror (L, narg, lua_tostring (L, -1));
}

/* Similar to record_check, but throws in case of failure. */
static Record *
record_get (lua_State *L, int narg)
{
  Record *record = record_check (L, narg);
  if (record == NULL)
    record_error (L, narg, NULL);

  return record;
}

int
lgi_record_2c (lua_State *L, GIBaseInfo *ri, int narg, gpointer *addr,
	       gboolean optional)
{
  Record *record;

  /* Check for nil. */
  if (optional && lua_isnoneornil (L, narg))
    {
      *addr = NULL;
      return 0;
    }

  /* Get record and check its type. */
  lgi_makeabs (L, narg);
  luaL_checkstack (L, 8, "");
  record = record_check (L, narg);
  if (ri)
    {
      /* Get repo type for ri GIBaseInfo. */
      lua_pushlightuserdata (L, &lgi_addr_repo);
      lua_rawget (L, LUA_REGISTRYINDEX);
      lua_getfield (L, -1, g_base_info_get_namespace (ri));
      lua_getfield (L, -1, g_base_info_get_name (ri));
      lua_getfenv (L, narg);

      /* Check, whether type fits. */
      if (record && !lua_equal (L, -1, -2))
	record = NULL;

      /* If there was some type problem, try whether type implements
	 custom conversion: 'res = type:_construct(arg)' */
      if (!record)
	{
	  lua_getfield (L, -2, "_construct");
	  if (!lua_isnil (L, -1))
	    {
	      lua_pushvalue (L, -3);
	      lua_pushvalue (L, narg);
	      lua_call (L, 2, 1);
	      if (!lua_isnil (L, -1))
		{
		  lua_replace (L, -5);
		  lua_pop (L, 3);
		  return lgi_record_2c (L, ri, -1, addr, optional) + 1;
		}
	    }
	  lua_pop (L, 1);
	}

      lua_pop (L, 4);
    }

  if (!record)
    record_error (L, narg, ri);

  *addr = record->addr;
  return 0;
}

GType
lgi_record_gtype (lua_State *L, int narg)
{
  GType gtype;
  record_get (L, narg);
  lua_getfenv (L, narg);
  lua_getfield (L, -1, "_gtype");
  gtype = lua_tonumber (L, -1);
  lua_pop (L, 2);
  return gtype;
}

static int
record_gc (lua_State *L)
{
  Record *record = record_get (L, 1);
  if (record->mode == LGI_RECORD_OWN)
    {
      /* Free the owned record. */
      GType gtype;
      lua_getfenv (L, 1);
      lua_getfield (L, -1, "_gtype");
      gtype = lua_tonumber (L, -1);
      g_assert (G_TYPE_IS_BOXED (gtype));
      g_boxed_free (gtype, record->addr);
    }
  else if (record->mode == LGI_RECORD_PARENT)
    /* Free the reference to the parent. */
    luaL_unref (L, LUA_REGISTRYINDEX, record->data.parent);

  return 0;
}

static int
record_tostring (lua_State *L)
{
  Record *record = record_get (L, 1);
  lua_pushfstring (L, "lgi.%s %p:", record->is_union ? "uni" : "rec",
		   record->addr);
  lua_getfenv (L, 1);
  lua_getfield (L, -1, "_name");
  lua_replace (L, -2);
  lua_concat (L, 2);
  return 1;
}

/* Worker method for __index and __newindex implementation. */
static int
record_access (lua_State *L)
{
  gboolean getmode = lua_isnone (L, 3);

  /* Check that 1st arg is a record and invoke one of the forms:
     result = type:_access(recordinstance, name)
     type:_access(recordinstance, name, val) */
  record_get (L, 1);
  lua_getfenv (L, 1);
  return lgi_marshal_access (L, getmode, 1, 2, 3);
}

static const struct luaL_Reg record_meta_reg[] = {
  { "__gc", record_gc },
  { "__tostring", record_tostring },
  { "__index", record_access },
  { "__newindex", record_access },
  { NULL, NULL }
};

/* Implements generic record creation. Lua prototype:
   recordinstance = core.record.new(structinfo|unioninfo) */
static int
record_new (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, LGI_GI_INFO);
  switch (g_base_info_get_type (*info))
    {
    case GI_INFO_TYPE_STRUCT:
    case GI_INFO_TYPE_UNION:
      {
	GType type = g_registered_type_info_get_g_type (*info);
	if (g_type_is_a (type, G_TYPE_CLOSURE))
	  {
	    /* Create closure instance wrapping 2nd argument and
	       return it. */
	    lgi_record_2lua (L, *info, lgi_gclosure_create (L, 2),
			     LGI_RECORD_OWN, 0);
	    return 1;
	  }

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
		lgi_marshal_val_2c (L, NULL, GI_TRANSFER_NOTHING,
				    &val, 3);
	      }

	    lgi_record_2lua (L, *info, g_boxed_copy (G_TYPE_VALUE, &val),
			     LGI_RECORD_OWN, 0);
	    if (G_IS_VALUE (&val))
	      g_value_unset (&val);
	    return 1;
	  }
	else
	  {
	    /* Create common struct. */
	    lgi_record_2lua (L, *info, NULL, LGI_RECORD_ALLOCATE, 0);
	    return 1;
	  }
	break;
      }

    default:
      g_assert_not_reached ();
    }
}

/* Checks whether given value is record. */
static int
record_typeof (lua_State *L)
{
  Record *record = record_check (L, 1);
  if (!record)
    return 0;
  lua_getfenv (L, 1);
  return 1;
}

/* Implements set/get field operation. Lua prototypes:
   res = core.record.field(recordinstance, fieldinfo)
   core.record.field(recordinstance, fieldinfo, newval) */
static int
record_field (lua_State *L)
{
  gboolean getmode;
  Record *record;

  /* Check, whether we are doing set or get operation. */
  getmode = lua_isnone (L, 3);

  /* Get record instance. */
  record = record_get (L, 1);

  /* Call field marshalling worker. */
  return lgi_marshal_field (L, record->addr, getmode, 1, 2, 3);
}

/* Returns contents of the GObject.Value record. */
static int
record_valueof (lua_State *L)
{
  GValue *val = record_get (L, 1)->addr;
  lgi_marshal_val_2lua (L, NULL, GI_TRANSFER_NOTHING, val);
  return 1;
}

static const struct luaL_Reg record_api_reg[] = {
  { "new", record_new },
  { "typeof", record_typeof },
  { "field", record_field },
  { "valueof", record_valueof },
  { NULL, NULL }
};

void
lgi_record_init (lua_State *L)
{
  /* Register record metatable. */
  lua_pushlightuserdata (L, &record_mt);
  lua_newtable (L);
  luaL_register (L, NULL, record_meta_reg);
  lua_rawset (L, LUA_REGISTRYINDEX);

  /* Create ref_cache. */
  lgi_cache_create (L, &record_cache, "v");

  /* Create 'record' API table in main core API table. */
  lua_newtable (L);
  luaL_register (L, NULL, record_api_reg);
  lua_setfield (L, -2, "record");
}
