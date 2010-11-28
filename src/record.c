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

/* Address of this field is used as lightuserdata identifier of
   metatable for Record objects (1st is full, 2nd is without __gc). */
static int record_mt_ref[2];

gpointer
lgi_record_2lua (lua_State *L, GIBaseInfo *info, gpointer addr,
		 LgiRecordMode mode, int parent)
{
  size_t size;
  Record *record;
  gboolean is_union;

  /* Convert 'parent' index to an absolute one. */
  luaL_checkstack (L, 7, "");
  lgi_makeabs (L, parent);

  /* NULL pointer results in 'nil'. */
  if (mode != LGI_RECORD_ALLOCATE && addr == NULL)
    {
      lua_pushnil (L);
      return NULL;
    }

  /* Prepare access to registry and cache. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti (L, -1, LGI_REG_CACHE);

  /* Check whether the record is already cached. */
  lua_pushlightuserdata (L, addr);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1) && mode != LGI_RECORD_PARENT)
    {
      /* Remove unneeded tables under our requested object. */
      lua_replace (L, -3);
      lua_pop (L, 1);

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
  lua_rawgeti (L, LUA_REGISTRYINDEX,
	       record_mt_ref [mode == LGI_RECORD_ALLOCATE
			      || mode == LGI_RECORD_PEEK]);
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
  lua_rawgeti (L, -4, LGI_REG_REPO);
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

  /* Clean up the stack; remove reg and cache tables from under our
     result. */
  lua_replace (L, -4);
  lua_pop (L, 2);
  return addr;
}

/* Checks that given argument is Record userdata and returns pointer
   to it. Returns NULL if narg has bad type. */
static Record *
record_check (lua_State *L, int narg, int stackalloc)
{
  /* Check using metatable that narg is really Record type. */
  Record *record = lua_touserdata (L, narg);
  luaL_checkstack (L, MIN (3, stackalloc), "");
  if (!lua_getmetatable (L, narg))
    return NULL;
  lua_rawgeti (L, LUA_REGISTRYINDEX, record_mt_ref [0]);
  if (!lua_equal (L, -1, -2))
    {
      lua_pop (L, 1);
      lua_rawgeti (L, LUA_REGISTRYINDEX, record_mt_ref [1]);
      if (!lua_equal (L, -1, -2))
	record = NULL;
    }

  lua_pop (L, 2);
  return record;
}

/* Similar to record_check, but throws in case of failure. */
static Record *
record_get (lua_State *L, int narg, int stackalloc)
{
  Record *record = record_check (L, narg, stackalloc);
  if (record == NULL)
    {
      lua_pushfstring (L, "lgi.record expected, got %s",
		       lua_typename (L, lua_type (L, narg)));
      luaL_argerror (L, narg, lua_tostring (L, -1));
    }

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
  record = record_check (L, narg, 9);
  if (ri)
    {
      /* Get repo type for ri GIBaseInfo. */
      lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
      lua_rawgeti (L, -1, LGI_REG_REPO);
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
		  lua_replace (L, -6);
		  lua_pop (L, 4);
		  return lgi_record_2c (L, ri, -1, addr, optional) + 1;
		}
	    }
	  lua_pop (L, 1);
	}

      lua_pop (L, 5);
    }

  *addr = record ? record->addr : NULL;
  return 0;
}

GType
lgi_record_gtype (lua_State *L, int narg)
{
  GType gtype;
  record_get (L, narg, 2);
  lua_getfenv (L, narg);
  lua_getfield (L, -1, "_gtype");
  gtype = lua_tonumber (L, -1);
  lua_pop (L, 2);
  return gtype;
}

static int
record_gc (lua_State *L)
{
  Record *record = record_get (L, 1, 2);
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
  Record *record = record_get (L, 1, 3);
  lua_pushfstring (L, "lgi.%s %p:", record->is_union ? "uni" : "rec",
		   record->addr);
  lua_getfenv (L, 1);
  lua_getfield (L, -1, "_name");
  lua_replace (L, -2);
  lua_concat (L, 2);
  return 1;
}

/* Implements generic record creation. Lua prototype:
   recordinstance = core.record.new(structinfo|unioninfo) */
static int
record_new (lua_State *L)
{
  lgi_record_2lua (L, record_get (L, 1, 3)->addr, NULL,
		   LGI_RECORD_ALLOCATE, 0);
  return 1;
}

/* Implements set/get field operation. Lua prototypes:
   res = core.record.field(recordinstance, fieldinfo)
   core.record.field(recordinstance, fieldinfo, newval) */
static int
record_field (lua_State *L)
{
  gboolean get;
  Record *record;
  GIFieldInfo *fi;
  GIFieldInfoFlags flags;
  GITypeInfo *ti;
  GIArgument *val;

  /* Check, whether we are doing set or get operation. */
  get = lua_isnone (L, 3);

  /* Get record and field instances. */
  record = record_get (L, 1, 2);
  fi = *(GIFieldInfo **) luaL_checkudata (L, 2, LGI_GI_INFO);

  /* Check, whether field is readable/writable. */
  flags = g_field_info_get_flags (fi);
  if ((flags & (get ? GI_FIELD_IS_READABLE : GI_FIELD_IS_WRITABLE)) == 0)
    {
      /* Prepare proper error message. */
      lua_getfenv (L, 1);
      lua_getfield (L, -1, "_name");
      luaL_error (L, "%s: field `%s' is not %s", lua_tostring (L, -1),
		  g_base_info_get_name (fi), get ? "readable" : "writable");
    }

  /* Map GIArgument to proper memory location, get typeinfo of the
     field and perform actual marshalling. */
  val = (GIArgument *) (((char *) record->addr)
			+ g_field_info_get_offset (fi));
  ti = g_field_info_get_type (fi);
  lgi_gi_info_new (L, ti);
  if (get)
    {
      lgi_marshal_arg_2lua (L, ti, GI_TRANSFER_NOTHING, val, 1,
			    FALSE, NULL, NULL);
      return 1;
    }
  else
    {
      lgi_marshal_arg_2c (L, ti, NULL, GI_TRANSFER_NOTHING, val, 3,
			  FALSE, NULL, NULL);
      return 0;
    }
}

/* Worker method for __index and __newindex implementation. */
static int
record_access (lua_State *L)
{
  gboolean get = lua_isnone (L, 3);

  /* Check that 1st arg is a record and invoke one of the forms:
     result = type:_access(type, recordinstance, name)
     type:_access(type, recordinstance, name, val) */
  record_get (L, 1, 7);
  lua_getfenv (L, 1);
  lua_getfield (L, -1, "_access");
  lua_pushvalue (L, -2);
  lua_pushvalue (L, 1);
  lua_pushvalue (L, 2);
  if (get)
    {
      lua_call (L, 3, 1);
      return 1;
    }
  else
    {
      lua_pushvalue (L, 3);
      lua_call (L, 4, 0);
      return 0;
    }
}

static const struct luaL_Reg record_meta_reg[] = {
  { "__gc", record_gc },
  { "__tostring", record_tostring },
  { "__index", record_access },
  { "__newindex", record_access },
  { NULL, NULL }
};

static const struct luaL_Reg record_api_reg[] = {
  { "field", record_field },
  { "new", record_new },
  { NULL, NULL }
};

void
lgi_record_init (lua_State *L)
{
  /* Register record metatables. */
  int i;
  for (i = 0; i < 2; i++)
    {
      lua_newtable (L);
      luaL_register (L, NULL, record_meta_reg + i);
      record_mt_ref [i] = luaL_ref (L, LUA_REGISTRYINDEX);
    }

  /* Create 'record' API table in main core API table. */
  lua_newtable (L);
  luaL_register (L, NULL, record_api_reg);
  lua_setfield (L, -2, "record");
}
