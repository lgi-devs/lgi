/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Management of structures and unions.
 */

#include <string.h>
#include "lgi.h"

/* Userdata containing struct reference. Table with structure type is
   attached as userdata environment. */
typedef struct _Struct
{
  /* Address of the structure data. */
  gpointer addr;

  /* Ownership mode of the structure. */
  LgiStructMode mode;

  /* If the structure is allocated 'on the stack', its data is here. */
  union
  {
    gchar data[1];
    int parent;
  } data;
} Struct;

/* Address of this field is used as lightuserdata identifier of
   metatable for _Struct objects. */
static int struct_mt_ref;

gpointer
lgi_struct_2lua (lua_State *L, GIStructInfo *info, gpointer addr,
		 LgiStructMode mode, int parent)
{
  size_t size;
  Struct *structure;

  /* Convert 'parent' index to an absolute one. */
  lgi_makeabs (L, parent);

  /* NULL pointer results in 'nil'. */
  if (mode != LGI_STRUCT_ALLOCATE && addr == NULL)
    {
      lua_pushnil (L);
      return NULL;
    }

  /* Prepare access to registry and cache. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti (L, -1, LGI_REG_CACHE);

  /* Check whether the structure is already cached. */
  lua_pushlightuserdata (L, addr);
  lua_rawget (L, -2);
  if (!lua_isnil (L, -1))
    {
      /* Remove unneeded tables under our requested object. */
      lua_replace (L, -3);
      lua_pop (L, 1);

      /* In case that we want to own the structure, make sure that the
	 ownership is properly updated. */
      structure = lua_touserdata (L, -1);
      g_assert (structure->addr == addr);
      if (mode == LGI_STRUCT_OWN)
	{
	  g_assert (mode != LGI_STRUCT_PARENT);
	  if (structure->mode == LGI_STRUCT_PEEK)
	    structure->mode = mode;
	}

      return addr;
    }
      
  /* Calculate size of the structure to allocate. */
  if (mode == LGI_STRUCT_ALLOCATE)
    size = G_STRUCT_OFFSET (Struct, data)
      +  (g_base_info_get_type (info) == GI_INFO_TYPE_STRUCT)
      ? g_struct_info_get_size (info) : g_union_info_get_size (info);
  else
    size = (parent == 0) ? G_STRUCT_OFFSET (Struct, data) : sizeof (Struct);

  /* Allocate new userdata for structure object, attach proper
     metatable. */
  structure = lua_newuserdata (L, size);
  lua_rawgeti (L, LUA_REGISTRYINDEX, struct_mt_ref);
  lua_setmetatable (L, -2);
  structure->addr = addr;
  structure->mode = mode;
  if (mode == LGI_STRUCT_ALLOCATE)
    memset (structure->data.data, 0, size - G_STRUCT_OFFSET (Struct, data));
  else if (mode == LGI_STRUCT_PARENT)
    structure->data.parent = parent;

  /* Get ref_repo table according to the 'info'. */
  lua_rawgeti (L, -3, LGI_REG_REPO);
  lua_getfield (L, -1, g_base_info_get_namespace (info));
  lua_getfield (L, -1, g_base_info_get_name (info));
  g_assert (!lua_isnil (L, -1));

  /* Attach found table as environment data for created Struct. */
  lua_setfenv (L, -4);
  lua_pop (L, 2);

  /* Store newly created structure into the cache. */
  lua_pushlightuserdata (L, addr);
  lua_pushvalue (L, -2);
  lua_rawset (L, -4);

  /* Clean up the stack; remove reg and cache tables from under our
     result. */
  lua_replace (L, -3);
  lua_pop (L, 1);
  return addr;
}

int
lgi_struct_2c (lua_State *L, GIStructInfo *si, int narg, gpointer *addr,
	       gboolean optional)
{
  return 0;
}

void
lgi_struct_init (lua_State *L)
{
}
