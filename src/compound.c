/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Management of compounds, i.e. structs, unions, objects interfaces, wrapped
 * into single Lua userdata block called 'compound'.
 */

#include <string.h>
#include "lgi.h"

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

/* Loads reg_typeinfo and ref_repo elements for compound arg on the stack.  */
static Compound *
compound_prepare (lua_State *L, int arg, gboolean throw)
{
  /* Check metatable.  Don't use luaL_checkudata, because we want better type
     specified in the error message in case of type mismatch. */
  if (lua_getmetatable (L, arg))
    {
      lua_getfield (L, LUA_REGISTRYINDEX, LGI_COMPOUND);
      int equal = lua_equal (L, -1, -2);
      lua_pop (L, 2);

      if (equal)
        {
          /* Type is fine, clean metatables from stack and do real
             preparation. */
          Compound *compound = lua_touserdata (L, arg);
          lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
          lua_rawgeti (L, -1, LGI_REG_TYPEINFO);
          lua_replace (L, -2);
          lua_rawgeti (L, -1, compound->ref_repo);
          g_assert (!lua_isnil (L, -1));
          return compound;
        }
    }

  /* Report error if requested. */
  if (throw)
    luaL_typerror (L, arg, "lgi.Object");

  return NULL;
}

static gboolean
compound_register (lua_State *L, GIBaseInfo* info, gpointer *addr,
                   gboolean owns, gboolean alloc_struct)
{
  Compound *compound;
  g_assert (addr != NULL);
  gsize size;
  GIInfoType info_type;

  /* Prepare access to registry and cache. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, lgi_regkey);
  lua_rawgeti (L, -1, LGI_REG_CACHE);

  /* NULL pointer results in 'nil' compound, unless 'allocate' is requested. */
  if (*addr == NULL)
    {
      if (!alloc_struct)
        {
          lua_pushnil (L);
          return TRUE;
        }
    }
  else
    {
      /* Check, whether the compound is not already in the cache. */
      lua_pushlightuserdata (L, *addr);
      lua_rawget (L, -2);
      if (!lua_isnil (L, -1))
        {
          lua_replace (L, -3);
          lua_pop (L, 1);
          return TRUE;
        }
      else
        lua_pop (L, 1);
    }

  /* Create and initialize new userdata instance. */
  size = 0;
  if (alloc_struct)
    {
      info_type = g_base_info_get_type (info);
      if (info_type == GI_INFO_TYPE_STRUCT)
        size = g_struct_info_get_size (info);
      else if (info_type == GI_INFO_TYPE_UNION)
        size = g_union_info_get_size (info);
    }
  compound = lua_newuserdata (L, G_STRUCT_OFFSET (Compound, data) + size);
  luaL_getmetatable (L, LGI_COMPOUND);
  lua_setmetatable (L, -2);
  if (alloc_struct)
    {
      *addr = compound->data;
      memset (compound->data, 0, size);
    }

  /* Load ref_repo reference to repo table of the object. */
  compound->ref_repo = LUA_REFNIL;
  lua_rawgeti (L, -3, LGI_REG_REPO);
  lua_getfield (L, -1, g_base_info_get_namespace(info));
  lua_getfield (L, -1, g_base_info_get_name(info));
  lua_replace (L, -3);
  lua_pop (L, 1);

  /* Store it to the typeinfo. */
  lua_rawgeti (L, -4, LGI_REG_TYPEINFO);
  lua_pushvalue (L, -2);
  compound->ref_repo = luaL_ref (L, -2);
  lua_pop (L, 2);
  compound->addr = *addr;
  compound->owns = owns;

  /* If we are storing owned gobject, make sure that we fully sink them.  We
     are not interested in floating refs. */
  if (g_base_info_get_type (info) == GI_INFO_TYPE_OBJECT &&
      g_object_is_floating (*addr))
    g_object_ref_sink (*addr);

  /* Store newly created compound to the cache. */
  lua_pushlightuserdata (L, compound);
  lua_pushvalue (L, -2);
  lua_rawset (L, -4);

  /* Clean up the stack; we still have 'registry' and 'cache' tables above our
     requested compound, so remove them. */
  lua_replace (L, -3);
  lua_pop (L, 1);
  return TRUE;
}

gboolean
lgi_compound_create (lua_State *L, GIBaseInfo *ii, gpointer addr, gboolean own)
{
  return compound_register (L, ii, &addr, own, FALSE);
}

gpointer
lgi_compound_struct_new (lua_State *L, GIBaseInfo *ii)
{
  /* Register struct, allocate space for it inside compound.  Mark is
     non-owned, because we do not need to free it in any way; its space will be
     reclaimed with compound itself. */
  gpointer addr = NULL;
  return compound_register (L, ii, &addr, FALSE, TRUE) ? addr : NULL;
}

gpointer
lgi_compound_object_new (lua_State *L, GIObjectInfo *oi, int argtable)
{
  gint n_params = 0;
  GParameter *params = NULL, *param;
  GIPropertyInfo *pi;
  GITypeInfo *ti;
  gpointer addr;

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

		  /* Initialize and load parameter value from the table
		     contents. */
		  ti = g_property_info_get_type (pi);
		  lgi_value_init (L, &param->value, ti);
		  lgi_value_load (L, &param->value, -1, ti);
		  g_base_info_unref (ti);
		}
	    }

	  /* Pop the repo class table. */
	  lua_pop (L, 1);
	}
    }

  /* Create the object. */
  addr = g_object_newv (g_registered_type_info_get_g_type (oi), n_params,
                        params);

  /* Free all parameters from params array. */
  for (param = params; n_params > 0; param++, n_params--)
    g_value_unset (&param->value);

  /* And wrap a nice userdata around it. */
  return compound_register (L, oi, &addr, TRUE, FALSE) ? addr : NULL;
}

static int
compound_gc (lua_State *L)
{
  Compound *compound = compound_prepare (L, 1, TRUE);
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
compound_tostring (lua_State *L)
{
  Compound *compound = compound_prepare (L, 1, TRUE);
  lua_pushfstring (L, "lgi %p:", compound);
  lua_rawgeti (L, -2, 0);
  lua_getfield (L, -1, "name");
  lua_replace (L, -2);
  lua_concat (L, 2);
  return 1;
}

/* Reports error related to given compound element. Expects
   compound_prepared' stack layout.*/
static int
compound_error (lua_State *L, const char *errmsg, int element)
{
  /* Prepare name of the compound. */
  lua_rawgeti (L, -2, 0);
  lua_getfield (L, -1, "name");
  return luaL_error (L, errmsg, lua_tostring (L, -1), 
		     lua_tostring (L, element));
}

gpointer
lgi_compound_get (lua_State *L, int index, GIBaseInfo *ii, gboolean optional)
{
  Compound *compound;
  GType requested_type, real_type;
  int vals, gottype;

  if (optional && lua_isnoneornil (L, index))
    return NULL;

  /* Check for type ancestry. */
  compound = compound_prepare (L, index, FALSE);
  if (compound != NULL)
    {
      lua_rawgeti (L, -1, 0);
      lua_getfield (L, -1, "gtype");
      real_type = lua_tointeger (L, -1);
      requested_type = g_registered_type_info_get_g_type (ii);
      lua_pop (L, 4);
      if (g_type_is_a (real_type, requested_type))
        return compound->addr;
    }
  else
    /* Remove items stored by compound_prepare(). */
    lua_pop (L, 2);

  /* Put exact requested type into the error message. */
  gottype = lua_type (L, index);
  vals = lgi_type_get_name (L, ii);
  lua_pushstring (L, " expected, got ");
  lua_pushstring (L, lua_typename (L, gottype));
  lua_concat (L, vals + 2);
  luaL_argerror (L, index - 1, lua_tostring (L, -1));
  return NULL;
}

/* Processes compound element of 'field' type. */
static int
process_field (lua_State* L, gpointer addr, GIFieldInfo* fi, int newval)
{
  GIArgument *val = G_STRUCT_MEMBER_P (addr, g_field_info_get_offset (fi));
  GITypeInfo *ti = g_field_info_get_type (fi);
  int flags = g_field_info_get_flags (fi);
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
process_property (lua_State *L, gpointer addr, GIPropertyInfo *pi, int newval)
{
  int vals = 0, flags = g_property_info_get_flags (pi);
  GITypeInfo *ti = g_property_info_get_type (pi);
  const gchar *name = g_base_info_get_name (pi);
  GValue val = {0};

  lgi_value_init (L, &val, ti);

  if (newval == -1)
    {
      if ((flags & G_PARAM_READABLE) == 0)
	{
	  g_base_info_unref (ti);
	  return luaL_argerror (L, 2, "not readable");
	}

      g_object_get_property ((GObject *) addr, name, &val);
      vals = lgi_value_store (L, &val, ti);
    }
  else
    {
      if ((flags & G_PARAM_WRITABLE) == 0)
	{
	  g_base_info_unref (ti);
	  return luaL_argerror (L, 2, "not writable");
	}

      vals = lgi_value_load (L, &val, 3, ti);
      g_object_set_property ((GObject *) addr, name, &val);
    }

  g_value_unset (&val);
  return vals;
}

static void
lgi_g_closure_destroy (gpointer user_data, GClosure *closure)
{
  lgi_closure_destroy (user_data);
}

/* Connects new handler for specified signal. */
static gulong
assign_signal (lua_State *L, GObject *obj, GISignalInfo *pi, int target,
               GQuark detail, gboolean after)
{
  gpointer lgi_closure, call_addr;
  GClosure *g_closure;
  gulong handler_id;
  guint signal_id;

  lgi_closure = lgi_closure_create (L, pi, target, FALSE, &call_addr);
  g_closure = g_cclosure_new (call_addr, lgi_closure, lgi_g_closure_destroy);
  signal_id = g_signal_lookup (g_base_info_get_name (pi),
                               G_TYPE_FROM_INSTANCE (obj));
  handler_id = g_signal_connect_closure_by_id (obj, signal_id, detail,
                                               g_closure, after);
  return handler_id;
}

/* Calls compound_prepare(arg1), checks element (arg2), and processes
   it; either reads it to stack (newval = -1) or sets it to value at
   newval stack. */
static int
process_element (lua_State *L, int newval)
{
  /* Load compound and element. */
  int vals = 0, type;
  Compound *compound = compound_prepare (L, 1, TRUE);
  lua_pushvalue (L, 2);
  lua_gettable (L, -2);
  type = lua_type (L, -1);
  if (type == LUA_TNIL)
      /* Not found. */
    return compound_error (L, "%s: no `%s'", 2);
  else
    {
      GIBaseInfo *ei = NULL;

      /* Try to extract BaseInfo from given field, if possible. */
      if (type == LUA_TUSERDATA && lua_getmetatable (L, -1))
	{
	  lua_getfield (L, LUA_REGISTRYINDEX, LGI_COMPOUND);
	  if (lua_rawequal (L, -1, -2))
	    ei = lgi_compound_get (L, -3, lgi_baseinfo_info, FALSE);
	  lua_pop (L, 2);
	}

      /* Special handling is for compound-userdata, which contain some
	 kind of baseinfo. */
      if (ei != NULL)
	{
	  switch (g_base_info_get_type (ei))
	    {
	    case GI_INFO_TYPE_FIELD:
	      vals = process_field (L, compound->addr, ei, newval);
	      break;

	    case GI_INFO_TYPE_PROPERTY:
	      vals = process_property (L, compound->addr, ei, newval);
	      break;

            case GI_INFO_TYPE_SIGNAL:
              if (newval != -1)
                assign_signal (L, compound->addr, ei, newval, 0, FALSE);
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
	      lua_pop (L, 1);
	      return compound_error (L, "%s: `%s' not writable", 2);
	    }
	  else
	    vals = 1;
	}
    }

  return vals;
}

static int
compound_index (lua_State *L)
{
  return process_element (L, -1);
}

static int
compound_newindex (lua_State *L)
{
  return process_element (L, 3);
}

const struct luaL_reg lgi_compound_reg[] = {
  { "__gc", compound_gc },
  { "__tostring", compound_tostring },
  { "__index", compound_index },
  { "__newindex", compound_newindex },
  { NULL, NULL }
};
