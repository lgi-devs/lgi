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
  LgiRecordMode mode;

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

  /* Convert 'parent' index to an absolute one. */
  lgi_makeabs (L, parent);

  /* NULL pointer results in 'nil'. */
  if (mode != LGI_RECORD_ALLOCATE && addr)
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
  if (!lua_isnil (L, -1))
    {
      /* Remove unneeded tables under our requested object. */
      lua_replace (L, -3);
      lua_pop (L, 1);

      /* In case that we want to own the record, make sure that the
	 ownership is properly updated. */
      record = lua_touserdata (L, -1);
      g_assert (record->addr == addr);
      if (mode == LGI_RECORD_OWN)
	{
	  g_assert (mode != LGI_RECORD_PARENT);
	  if (record->mode == LGI_RECORD_PEEK)
	    record->mode = mode;
	}

      return addr;
    }

  /* Calculate size of the record to allocate. */
  if (mode == LGI_RECORD_ALLOCATE)
    size = G_STRUCT_OFFSET (Record, data)
      +  (g_base_info_get_type (info) == GI_INFO_TYPE_STRUCT)
      ? g_struct_info_get_size (info) : g_union_info_get_size (info);
  else
    size = (parent == 0) ? G_STRUCT_OFFSET (Record, data) : sizeof (Record);

  /* Allocate new userdata for record object, attach proper
     metatable. */
  record = lua_newuserdata (L, size);
  lua_rawgeti (L, LUA_REGISTRYINDEX,
	       record_mt_ref [mode == LGI_RECORD_ALLOCATE
			      || mode == LGI_RECORD_PEEK]);
  lua_setmetatable (L, -2);
  record->addr = addr;
  record->mode = mode;
  if (mode == LGI_RECORD_ALLOCATE)
    memset (record->data.data, 0, size - G_STRUCT_OFFSET (Record, data));
  else if (mode == LGI_RECORD_PARENT)
    {
      /* Store reference to the parent argument. */
      lua_pushvalue (L, parent);
      record->data.parent = luaL_ref (L, LUA_REGISTRYINDEX);
    }

  /* Get ref_repo table according to the 'info'. */
  lua_rawgeti (L, -3, LGI_REG_REPO);
  lua_getfield (L, -1, g_base_info_get_namespace (info));
  lua_getfield (L, -1, g_base_info_get_name (info));
  g_assert (!lua_isnil (L, -1));

  /* Attach found table as environment data for created Record. */
  lua_setfenv (L, -4);
  lua_pop (L, 2);

  /* Store newly created record into the cache. */
  lua_pushlightuserdata (L, addr);
  lua_pushvalue (L, -2);
  lua_rawset (L, -4);

  /* Clean up the stack; remove reg and cache tables from under our
     result. */
  lua_replace (L, -3);
  lua_pop (L, 1);
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
  lua_getmetatable (L, narg);
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
		  lua_replace (L, -5);
		  lua_pop (L, 4);
		  return lgi_record_2c (L, ri, -1, addr, optional) + 1;
		}
	    }
	  lua_pop (L, 1);
	}

      lua_pop (L, 5);
    }

  *addr = record ? record->addr : NULL;
  lua_pop (L, 1);
  return 0;
}

static int
record_gc (lua_State *L)
{
  Record *record = record_check (L, 1, 3);
  if (record && record->mode == LGI_RECORD_OWN)
    {
      /* Free the owned record. */
      GType gtype;
      lua_getfenv (L, 1);
      lua_rawgeti (L, -1, 0);
      lua_getfield (L, -1, "gtype");
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
  Record *record = record_check (L, 1, 3);
  lua_pushfstring (L, "lgi.rec %p:", record->addr);
  lua_getfenv (L, 1);
  lua_rawgeti (L, -1, 0);
  lua_getfield (L, -1, "name");
  lua_replace (L, -3);
  lua_pop (L, 1);
  lua_concat (L, 2);
  return 1;
}

static const struct luaL_Reg record_meta_reg[] = {
  { "__gc", record_gc },
  { "__tostring", record_tostring },
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
}
