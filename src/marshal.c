/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 * License: MIT.
 *
 * Implements marshalling, i.e. transferring values between Lua and GLib/C.
 */

#include "lgi.h"

/* Marshals simple types to C.  Simple are number and strings. */
static int
marshal_2c_simple(lua_State* L, GITypeTag tag, GArgument* val, int narg,
		  gboolean optional)
{
  int nret = 1;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)		\
      case tag:							\
	val->argf = (optional && lua_isnoneornil(L, narg)) ?	\
	  (ctype)0 : (ctype)check(L, narg);
	break;
#include "decltype.h"

    default:
      nret = 0;
    }

  return nret;
}

/* Marshalls single value from Lua to GLib/C. */
void lgi_marshal_2c(lua_State* L, GITypeInfo* ti, GArgument* val, int narg,
 		    gboolean optional, GICallableInfo* ci, GArgument* args)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  if (marshal_2c_simple(L, tag, val, narg, optional))
    {
      switch (tag)
	{
	case GI_TYPE_TAG_VOID:
	  break;

	case GI_TYPE_TAG_INTERFACE:
	  {
	    GIBaseInfo* ii = g_type_info_get_interface(ti);
	    GIInfoType type = g_base_info_get_type(ii);
	    switch (type)
	      {
	      case GI_INFO_TYPE_ENUM:
	      case GI_INFO_TYPE_FLAGS:
		/* Directly store underlying value. */
		marshal_2c_simple(L, g_enum_info_get_storage_type(ii), val,
				  narg, optional);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_OBJECT:
	      case GI_INFO_TYPE_INTERFACE:
		val->v_pointer = lgi_compound_get(L, narg, ii, optional);
		break;

	      default:
		g_warning("unable to marshal iface type `%d'", (int)type);
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	default:
	  g_warning("unable to marshal type with tag `%d'", (int)tag);
	}
    }
}

/* Marshals simple types to Lua.  Simple are number and
   strings. Returns 1 if value was handled, 0 otherwise. */
static int
marshal_2lua_simple(lua_State* L, GITypeTag tag, GArgument* val, gboolean own)
{
  int nret = 1;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)		\
      case tag:							\
	push(L, val->argf);					\
	if (own)						\
	  dtor(val->argf);					\
	break;
#include "decltype.h"

    default:
      nret = 0;
    }

  return nret;
}

/* Marshalls single value from GLib/C to Lua.  Returns 1 if something
   was pushed to the stack. */
int lgi_marshal_2lua(lua_State* L, GITypeInfo* ti, GArgument* val, gboolean own,
		     GICallableInfo* ci, GArgument* args)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  int nret = marshal_2lua_simple(L, tag, val, own);
  if (nret == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_VOID:
	  break;

	case GI_TYPE_TAG_INTERFACE:
	  {
	    GIBaseInfo* ii = g_type_info_get_interface(ti);
	    GIInfoType type = g_base_info_get_type(ii);
	    switch (type)
	      {
	      case GI_INFO_TYPE_ENUM:
	      case GI_INFO_TYPE_FLAGS:
		/* Directly store underlying value. */
		nret = marshal_2lua_simple(L, g_enum_info_get_storage_type(ii),
					   val, own);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_OBJECT:
	      case GI_INFO_TYPE_INTERFACE:
		nret = lgi_compound_create(L, ii, val->v_pointer, own);
		break;

	      default:
		g_warning("unable to marshal iface type `%d'", (int)type);
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	default:
	  g_warning("unable to marshal type with tag `%d'", (int)tag);
	}
    }

  return nret;
}
