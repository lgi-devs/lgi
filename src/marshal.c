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
  int nret = marshal_2c_simple(L, tag, val, narg, optional);
  if (nret == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_VOID:
	  break;

	default:
	  g_warning("unable to marshal type with tag `%d'", (int)tag);
	}
    }
}

/* Marshals simple types to/from lua.  Simple are number and strings. Returns 1
   if value was handled, 0 otherwise. */
static int
marshal_2lua_simple(lua_State* L, GITypeInfo* ti, GArgument* val, gboolean own,
		    GITypeTag tag)
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
  int nret = marshal_2lua_simple(L, ti, val, own, tag);
  if (nret == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_VOID:
	  break;

	default:
	  g_warning("unable to marshal type with tag `%d'", (int)tag);
	}
    }

  return nret;
}
