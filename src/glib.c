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

/* Initializes type of GValue to specified ti. */
void
lgi_value_init (lua_State *L, GValue *val, GITypeInfo *ti)
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
int
lgi_value_load (lua_State *L, GValue *val, int narg, GITypeInfo *ti)
{
  int vals = 1;
  switch (g_type_info_get_tag (ti))
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 val_type, val_get, val_set, ffitype)           \
      case tag:							\
	val_set (val, check (L, narg));				\
	break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface (ti);
	switch (g_base_info_get_type (ii))
	  {
	  case GI_INFO_TYPE_ENUM:
	    g_value_set_enum (val, luaL_checkinteger (L, narg));
	    break;

	  case GI_INFO_TYPE_FLAGS:
	    g_value_set_flags (val, luaL_checkinteger (L, narg));
	    break;

	  case GI_INFO_TYPE_OBJECT:
	    g_value_set_object (val, lgi_compound_get (L, narg, ii, FALSE));
	    break;

	  case GI_INFO_TYPE_STRUCT:
	    return luaL_error (L, "don't know how to handle struct->GValue");

	  default:
	    vals = 0;
	  }
	g_base_info_unref (ii);
      }
      break;

    default:
      vals = 0;
    }

  return vals;
}

/* Pushes GValue content to stack, assumes that value is of ii type. */
int
lgi_value_store (lua_State *L, GValue *val, GITypeInfo *ti)
{
  int vals = 1;
  switch (g_type_info_get_tag (ti))
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 val_type, val_get, val_set, ffitype)           \
      case tag:							\
	push (L, val_get (val));                                \
	break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface (ti);
	switch (g_base_info_get_type (ii))
	  {
	  case GI_INFO_TYPE_ENUM:
	    lua_pushinteger (L, g_value_get_enum (val));
	    break;

	  case GI_INFO_TYPE_FLAGS:
	    lua_pushinteger (L, g_value_get_flags (val));
	    break;

	  case GI_INFO_TYPE_OBJECT:
            vals = lgi_compound_create (L, ii, g_value_dup_object (val), TRUE) ?
              1 : 0;
	    break;

	  case GI_INFO_TYPE_STRUCT:
	    return luaL_error (L, "don't know how to handle GValue->struct");

	  default:
	    vals = 0;
	  }
	g_base_info_unref (ii);
      }
      break;

    default:
      vals = 0;
    }

  return vals;
}
