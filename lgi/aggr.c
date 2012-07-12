/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implementation of aggregate, common parts of compound and C-arrays
 * (common parts are mainly related to caching and parent relationship
 * keeping).
 */

#include <string.h>
#include "lgi.h"

/* userdata with aggregate. */
typedef struct _Aggr
{
  /* Aggregate's data. */
  LgiAggregate data;

  /* If the aggregate is allocated 'inline', its data is
     here. Anonymous union makes sure that data is properly aligned to
     hold (hopefully) any structure. */
  union {
    gchar payload[1];
    double align_double;
    long align_long;
    gpointer align_ptr;
  };
} Aggr;

/* lightuserdata key to cache table containing
   lightuserdata(aggr->addr) -> weak(aggr) */
static int aggr_cache;

/* lightuserdata key to table containing weak(aggr) -> parent */
static int aggr_parent;

LgiAggregate *
lgi_aggr_find (lua_State *L, gpointer addr, int parent)
{
  Aggr *aggr;

  /* Avoid looking up records with parent, because child records might
     be aliased to the same address, but still they are different than
     parent records. */
  if (parent != 0)
    return NULL;

  lua_rawgetp (L, LUA_REGISTRYINDEX, &aggr_cache);
  lua_rawgetp (L, -1, addr);
  if (lua_isnil (L, -1))
    {
      lua_pop (L, 2);
      return NULL;
    }

  /* Remove the cache table and return retrieved record. */
  lua_remove (L, -2);
  aggr = lua_touserdata (L, -1);
  return &aggr->data;
}

LgiAggregate *
lgi_aggr_create (lua_State *L, gpointer mt,
		 gpointer addr, int size, int parent)
{
  Aggr *aggr;

  /* Create new aggregate. */
  size += G_STRUCT_OFFSET (Aggr, payload);
  aggr = lua_newuserdata (L, size);
  memset (aggr, 0, size);
  if (addr != NULL)
    aggr->data.addr = addr;
  else
    {
      aggr->data.is_inline = TRUE;
      aggr->data.addr = aggr->payload;
    }

  /* Assign metatable to it. */
  lua_rawgetp (L, LUA_REGISTRYINDEX, mt);
  lua_setmetatable (L, -2);

  if (parent == 0)
    {
      /* Store new aggregate into the lookup cache. */
      lua_rawgetp (L, LUA_REGISTRYINDEX, &aggr_cache);
      lua_pushvalue (L, -2);
      lua_rawsetp (L, -2, aggr->data.addr);
    }
  else
    {
      /* Create entry in the parent table. */
      lua_rawgetp (L, LUA_REGISTRYINDEX, &aggr_parent);
      lua_pushvalue (L, -2);
      lua_pushvalue (L, parent);
      lua_rawset (L, -3);
    }
  lua_pop (L, 1);
  return &aggr->data;
}

LgiAggregate *
lgi_aggr_get (lua_State *L, int narg, gpointer mt)
{
  Aggr *aggr = lua_touserdata (L, narg);
  if (mt != NULL)
    {
      if (G_UNLIKELY (!lua_getmetatable (L, narg)))
	aggr = NULL;
      else
	{
	  lua_rawgetp (L, LUA_REGISTRYINDEX, mt);
	  if (G_UNLIKELY (!lua_equal (L, -1, -2)))
	    aggr = NULL;
	}

      lua_pop (L, 2);
    }

  if (G_UNLIKELY (aggr == NULL))
    return NULL;

  return &aggr->data;
}

void
lgi_aggr_init (lua_State *L)
{
  /* Create caches and indices. */
  lgi_cache_create (L, &aggr_cache, "v");
  lgi_cache_create (L, &aggr_parent, "k");
}
