/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Management of structures and unions (i.e. records).
 */

#include <string.h>
#include "lgi.h"

/* Available record store modes. */
typedef enum _RecordStore
  {
    /* We do not have ownership of the record. */
    RECORD_STORE_EXTERNAL,

    /* Record is stored in data section of Record proxy itself. */
    RECORD_STORE_EMBEDDED,

    /* Record is placed inside some other (parent) record.  In order
       to keep parent record alive, parent record is stored in
       LUA_REGISTRYINDEX table, keyed by lightuserdata of address of
       this Record object. */
    RECORD_STORE_NESTED,

    /* Record is allocated by its GLib means and must be freed (by
       g_boxed_free). */
    RECORD_STORE_ALLOCATED,
  } RecordStore;

/* Userdata containing record reference. Table with record type is
   attached as userdata environment. */
typedef struct _Record
{
  /* Address of the record memory data. */
  gpointer addr;

  /* Store mode of the record. TODO: Might be possible to stuff it
     into 2 lowest bits of addr, although it is a bit hacky. */
  RecordStore store;

  /* If the record is allocated 'on the stack', its data is here. */
  gchar data[1];
} Record;

/* lightuserdata key to LUA_REGISTRYINDEX containing metatable for
   record. */
static int record_mt;

/* lightuserdata key to cache table containing
   lightuserdata(record->addr) -> weak(record) */
static int record_cache;

/* lightuserdata key to cache table containing
   recordproxy(weak) -> parent */
static int parent_cache;

gpointer
lgi_record_new (lua_State *L, GIBaseInfo *ri)
{
  Record *record;
  size_t size;

  luaL_checkstack (L, 4, "");

  /* Calculate size of the record to allocate. */
  size = G_STRUCT_OFFSET (Record, data)
    + ((g_base_info_get_type (ri) == GI_INFO_TYPE_UNION)
       ? g_union_info_get_size (ri) : g_struct_info_get_size (ri));

  /* Allocate new userdata for record object, attach proper
     metatable. */
  record = lua_newuserdata (L, size);
  lua_pushlightuserdata (L, &record_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);
  record->addr = record->data;
  memset (record->addr, 0, size - G_STRUCT_OFFSET (Record, data));
  record->store = RECORD_STORE_EMBEDDED;

  /* Get ref_repo table, attach it as an environment. */
  lgi_type_get_repotype (L, G_TYPE_NONE, ri);
  lua_setfenv (L, -2);

  /* Store newly created record into the cache. */
  lua_pushlightuserdata (L, &record_cache);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_pushlightuserdata (L, record->addr);
  lua_pushvalue (L, -3);
  lua_rawset (L, -3);
  lua_pop (L, 1);
  return record->addr;
}

void
lgi_record_2lua (lua_State *L, gpointer addr, gboolean own, int parent)
{
  Record *record;

  luaL_checkstack (L, 5, "");

  /* NULL pointer results in 'nil'. */
  if (addr == NULL)
    {
      lua_pop (L, 1);
      lua_pushnil (L);
      return;
    }

  /* Convert 'parent' index to an absolute one. */
  lgi_makeabs (L, parent);

  /* Prepare access to cache. */
  lua_pushlightuserdata (L, &record_cache);
  lua_rawget (L, LUA_REGISTRYINDEX);

  /* Check whether the record is already cached. */
  lua_pushlightuserdata (L, addr);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1) && parent == 0)
    {
      /* Remove unneeded tables under our requested object. */
      lua_replace (L, -3);
      lua_pop (L, 1);

      /* In case that we want to own the record, make sure that the
	 ownership is properly updated. */
      record = lua_touserdata (L, -1);
      g_assert (record->addr == addr);
      if (own && record->store == RECORD_STORE_EXTERNAL)
	record->store = RECORD_STORE_ALLOCATED;

      return;
    }

  /* Allocate new userdata for record object, attach proper
     metatable. */
  record = lua_newuserdata (L, G_STRUCT_OFFSET (Record, data));
  lua_pushlightuserdata (L, &record_mt);
  lua_rawget (L, LUA_REGISTRYINDEX);
  lua_setmetatable (L, -2);
  record->addr = addr;
  if (parent != 0)
    {
      /* Store reference to the parent argument into parent reference
	 cache. */
      lua_pushlightuserdata (L, &parent_cache);
      lua_rawget (L, LUA_REGISTRYINDEX);
      lua_pushvalue (L, -2);
      lua_pushvalue (L, parent);
      lua_rawset (L, -3);
      lua_pop (L, 1);
      record->store = RECORD_STORE_NESTED;
    }
  else
    {
      if (!own)
	{
	  /* Check, whether refrepo table specifies custom _refsink
	     function. */
	  void (*refsink_func)(gpointer) =
	    lgi_gi_load_function (L, -4, "_refsink");
	  if (refsink_func)
	    {
	      refsink_func(addr);
	      own = TRUE;
	    }
	}

      record->store = own ? RECORD_STORE_ALLOCATED : RECORD_STORE_EXTERNAL;
    }

  /* Assign refrepo table (on the stack when we are called) as
     environment for our proxy. */
  lua_pushvalue (L, -4);
  lua_setfenv (L, -2);

  /* Store newly created record into the cache. */
  if (parent == 0)
    {
      lua_pushlightuserdata (L, addr);
      lua_pushvalue (L, -2);
      lua_rawset (L, -5);
    }

  /* Clean up the stack; remove cache table from under our result, and
     remove also typetable which was present when we were called. */
  lua_replace (L, -4);
  lua_pop (L, 2);
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
record_error (lua_State *L, int narg, const gchar *expected_name)
{
  luaL_checkstack (L, 2, "");
  lua_pushstring (L, lua_typename (L, lua_type (L, narg)));
  lua_pushfstring (L, "%s expected, got %s",
		   expected_name ? expected_name : "lgi.record",
		   lua_tostring (L, -1));
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

gpointer
lgi_record_2c (lua_State *L, int narg, gboolean optional, gboolean nothrow)
{
  Record *record;

  /* Check for nil. */
  if (optional && lua_isnoneornil (L, narg))
    return NULL;

  /* Get record and check its type. */
  lgi_makeabs (L, narg);
  luaL_checkstack (L, 4, "");
  record = record_check (L, narg);
  if (!lua_isnil (L, -1))
    {
      /* Check, whether type fits. */
      lua_getfenv (L, narg);
      if (record && !lua_equal (L, -1, -2))
	record = NULL;

      lua_pop (L, 1);
    }

  if (!nothrow && !record)
    {
      const gchar *name = NULL;
      if (!lua_isnil (L, -1))
	{
	  lua_getfield (L, -1, "_name");
	  name = lua_tostring (L, -1);
	}
      record_error (L, narg, name);
    }

  lua_pop (L, 1);
  return record ? record->addr : NULL;
}

static int
record_gc (lua_State *L)
{
  Record *record = record_get (L, 1);
  if (record->store == RECORD_STORE_ALLOCATED)
    {
      /* Free the owned record. */
      GType gtype;
      lua_getfenv (L, 1);
      lua_getfield (L, -1, "_gtype");
      gtype = lua_tonumber (L, -1);
      if (G_TYPE_IS_BOXED (gtype))
	g_boxed_free (gtype, record->addr);
      else
	{
	  /* Use custom _free function. */
	  void (*free_func)(gpointer) = lgi_gi_load_function (L, -2, "_free");
	  g_assert (free_func);
	  free_func (record->addr);
	}
    }
  else if (record->store == RECORD_STORE_NESTED)
    {
      /* Free the reference to the parent. */
      lua_pushlightuserdata (L, record);
      lua_pushnil (L);
      lua_rawset (L, LUA_REGISTRYINDEX);
    }

  return 0;
}

static int
record_tostring (lua_State *L)
{
  Record *record = record_get (L, 1);
  lua_pushfstring (L, "lgi.rec %p:", record->addr);
  lua_getfenv (L, 1);
  lua_getfield (L, -1, "_name");
  lua_replace (L, -2);
  if (!lua_isnil (L, -1))
    lua_concat (L, 2);
  else
    lua_pop (L, 1);
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

/* Worker method for __len implementation. */
static int
record_len (lua_State *L)
{
  /* Check record, get its typetable and try to invoke _len method. */
  record_get (L, 1);
  lua_getfenv (L, 1);
  lua_getfield (L, -1, "_len");
  if (lua_isnil (L, -1))
    {
      lua_getfield (L, -2, "_name");
      return luaL_error (L, "`%s': attempt to get length", 
			 lua_tostring (L, -1));
    }
  lua_pushvalue (L, 1);
  lua_call (L, 1, 1);
  return 1;
}

static const struct luaL_Reg record_meta_reg[] = {
  { "__gc", record_gc },
  { "__tostring", record_tostring },
  { "__index", record_access },
  { "__newindex", record_access },
  { "__len", record_len },
  { NULL, NULL }
};

/* Implements generic record creation. Creates new record instance,
   unless 'addr' argument (lightuserdata or integer) is specified, in
   which case wraps specified address as record.  Lua prototype:

   recordinstance = core.record.new(structinfo|unioninfo[, addr[, own]])

   own (default false) means whether Lua takes record ownership
   (i.e. if it tries to deallocate the record when created Lua proxy
   dies). */
static int
record_new (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, LGI_GI_INFO);
  GIInfoType type = g_base_info_get_type (*info);
  luaL_argcheck (L, type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_UNION,
		 1, "record expected");
  if (lua_isnoneornil (L, 2))
    /* Create new record instance. */
    lgi_record_new (L, *info);
  else
    {
      /* Wrap record at existing address. */
      gpointer addr = (lua_type (L, 2) == LUA_TLIGHTUSERDATA)
	? addr = lua_touserdata (L, 2)
	: (gpointer) luaL_checkinteger (L, 2);
      gboolean owned = lua_toboolean (L, 3);
      lgi_type_get_repotype (L, G_TYPE_NONE, *info);
      g_assert (!lua_isnil (L, -1));
      lgi_record_2lua (L, addr, owned, 0);
    }
  return 1;
}

static const char* const query_modes[] = { "gtype", "repo", "addr", NULL };

/* Returns specific information mode about given record.  Lua prototype:
   res = record.query(instance, mode)
   Supported 'mode' strings are:

   'gtype': retrns real gtype of this instance, G_TYPE_INVALID when it
	    is not boxed.
   'repo':  returns repotable of this instance.
   'addr': returns address of the object.  If 3rd argument is either
           gtype or info, checks, whether record conforms to the specs
           and if not, throws an error.  */
static int
record_query (lua_State *L)
{
  Record *record;
  int mode = luaL_checkoption (L, 2, query_modes[0], query_modes);
  if (mode < 2)
    {
      record = record_check (L, 1);
      if (!record)
	return 0;

      lua_getfenv (L, 1);
      if (mode == 0)
	{
	  if (lua_isnil (L, -1))
	    return 0;

	  lua_getfield (L, -1, "_gtype");
	}
      return 1;
    }
  else
    {
      GType gtype = G_TYPE_INVALID;
      GIBaseInfo **info = lgi_udata_test (L, 3, LGI_GI_INFO);
      if (info == NULL)
	gtype = lgi_type_get_gtype (L, 3);
      lgi_type_get_repotype (L, gtype, info != NULL ? *info : NULL);
      lua_pushlightuserdata (L, lgi_record_2c (L, 1, TRUE, FALSE));
      return 1;
    }
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

/* Casts given record to another record type.  Lua prototype:
   res = core.record.cast(recordinstance, targettypetable) */
static int
record_cast (lua_State *L)
{
  Record *record = record_get (L, 1);
  luaL_checktype (L, 2, LUA_TTABLE);
  lgi_record_2lua (L, record->addr, FALSE, 1);
  return 1;
}

static const struct luaL_Reg record_api_reg[] = {
  { "new", record_new },
  { "query", record_query },
  { "field", record_field },
  { "cast", record_cast },
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

  /* Create caches. */
  lgi_cache_create (L, &record_cache, "v");
  lgi_cache_create (L, &parent_cache, "k");

  /* Create 'record' API table in main core API table. */
  lua_newtable (L);
  luaL_register (L, NULL, record_api_reg);
  lua_setfield (L, -2, "record");
}
