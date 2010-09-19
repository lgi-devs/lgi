/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Code dealing with GLib-specific stuff, mainly GValue marshalling and Lua
 * GClosure handling and implementation.
 */

#include "lgi.h"

void
lgi_value_init (lua_State *L, GValue *val, GITypeInfo *ti)
{
  GITypeTag tag = g_type_info_get_tag (ti);
  switch (tag)
    {
    case GI_TYPE_TAG_VOID:
      g_value_init (val, G_TYPE_NONE);
      break;

#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup, \
		 val_type, val_get, val_set, ffitype)           \
    case tag:                                                   \
      g_value_init (val, val_type);                             \
      break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface (ti);
	if (GI_IS_REGISTERED_TYPE_INFO (ii))
	  {
	    g_value_init (val, g_registered_type_info_get_g_type (ii));
	    g_base_info_unref (ii);
	  }
	else
	  {
	    int type = g_base_info_get_type (ii);
	    g_base_info_unref (ii);
	    luaL_error (L, "value_init: bad ti.iface.type=%d", type);
	  }
      }
      break;

      /* TODO: Handle arrays. */

    default:
      luaL_error (L, "value_init: bad ti.tag=%d", (int) tag);
    }
}

int
lgi_value_load (lua_State *L, GValue *val, int narg)
{
  GType type = G_VALUE_TYPE (val);
  if (type == G_TYPE_NONE)
    return 0;
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 gtype, val_get, val_set, ffitype)		\
  else if (type == gtype)					\
    {								\
      val_set (val, check (L, narg));				\
      return 1;							\
    }
#define DECLTYPE_KEY_BY_GTYPE
#include "decltype.h"

  /* Handle other cases. */
  switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_ENUM:
      g_value_set_enum (val, luaL_checkinteger (L, narg));
      return 1;

    case G_TYPE_FLAGS:
      g_value_set_flags (val, luaL_checkinteger (L, narg));
      return 1;

    case G_TYPE_OBJECT:
      g_value_set_object (val, lgi_compound_get (L, narg, type, FALSE));
      return 1;

    case G_TYPE_BOXED:
      g_value_set_boxed (val, lgi_compound_get (L, narg, type, FALSE));
      return 1;

    default:
      break;
    }

  return luaL_error (L, "g_value_set: no handling of %s(%s))",
		     g_type_name (type),
		     g_type_name (G_TYPE_FUNDAMENTAL (type)));
}

int
lgi_value_store (lua_State *L, const GValue *val)
{
  GType type = G_VALUE_TYPE (val);
  if (type == G_TYPE_NONE)
    return 0;
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 gtype, val_get, val_set, ffitype)		\
  else if (type == gtype)					\
    {								\
      push (L, val_get (val));					\
      return 1;							\
    }
#define DECLTYPE_KEY_BY_GTYPE
#include "decltype.h"

  /* Handle other cases. */
  switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_ENUM:
      lua_pushinteger (L, g_value_get_enum (val));
      return 1;

    case G_TYPE_FLAGS:
      lua_pushinteger (L, g_value_get_flags (val));
      return 1;

    case G_TYPE_OBJECT:
    case G_TYPE_BOXED:
      {
	GIBaseInfo *bi = g_irepository_find_by_gtype (NULL, type);
	if (bi != NULL)
	  {
	    gpointer obj = GI_IS_OBJECT_INFO (bi) ?
	      g_value_dup_object (val) : g_value_dup_boxed (val);
	    int vals = lgi_compound_create (L, bi, obj, TRUE);
	    g_base_info_unref (bi);
	    return vals;
	  }
	break;
      }

    default:
      break;
    }

  return luaL_error (L, "g_value_get: no handling or  %s(%s)",
		     g_type_name (type),
		     g_type_name (G_TYPE_FUNDAMENTAL (type)));
}
