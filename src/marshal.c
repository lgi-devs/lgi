/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 * License: MIT.
 *
 * Implements marshalling, i.e. transferring values between Lua and GLib/C.
 */

#include "lgi.h"

/* Returns int value of specified parameter.  If specified parameter does not
   exist or its value cannot be converted to int, FALSE is returned. */
static gboolean
get_int_param(GICallableInfo* ci, GArgument* args, int param, int *val)
{
  param--;
  if (param >= 0 && param < g_callable_info_get_n_args(ci))
    {
      GIArgInfo ai;
      GITypeInfo ti;
      g_callable_info_load_arg(ci, param, &ai);
      g_arg_info_load_type(&ai, &ti);
      switch (g_type_info_get_tag(&ti))
	{
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)		\
	  case tag:                                             \
	    *val = (int) args[param].argf;			\
	    return TRUE;
#define DECLTYPE_NUMERIC_ONLY
#include "decltype.h"

	default:
	  break;
	}
    }

  return FALSE;
}

/* Retrieves sizeof() specified type. */
static gsize
get_type_size(GITypeTag tag)
{
  gsize size;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,	\
		 valtype, valget, valset, ffitype)              \
      case tag:							\
	size = sizeof(ctype);					\
	break;
#include "decltype.h"

    default:
      size = sizeof(gpointer);
    }

  return size;
}

/* Marshals simple types to C.  Simple are number and strings. */
static gboolean
marshal_2c_simple(lua_State* L, GITypeTag tag, GArgument* val, int narg,
		  gboolean optional)
{
  gboolean handled = TRUE;
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
      handled = FALSE;
    }

  return handled;
}

/* Marshalls single value from Lua to GLib/C. */
void lgi_marshal_2c(lua_State* L, GITypeInfo* ti, GArgument* val, int narg,
 		    gboolean optional, GICallableInfo* ci, GArgument* args)
{
  GITypeTag tag = g_type_info_get_tag(ti);
  if (!marshal_2c_simple(L, tag, val, narg, optional))
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
		g_warning("unable to marshal iface type `%d'", (int) type);
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	default:
	  g_warning("unable to marshal type with tag `%d'", (int) tag);
	}
    }
}

/* Marshals simple types to Lua.  Simple are number and
   strings. Returns TRUE if value was handled, 0 otherwise. */
static gboolean
marshal_2lua_simple(lua_State* L, GITypeTag tag, GArgument* val, gboolean own)
{
  gboolean handled = TRUE;
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
      handled = FALSE;
    }

  return FALSE;
}

static gboolean
marshal_2lua_carray(lua_State* L, GITypeInfo* ti, GArgument* val, 
		    GITransfer xfer, GICallableInfo* ci, GArgument* args)
{
  gint len, index;

  /* First of all, find out the length of the array. */
  if (g_type_info_is_zero_terminated(ti))
    len = -1;
  else
    {
      len = g_type_info_get_array_fixed_size(ti);
      if (len == -1)
	{
	  /* Length of the array is dynamic, get it from other argument. */
	  if (ci == NULL)
	    return FALSE;

	  len = g_type_info_get_array_length(ti);
	  if (!get_int_param(ci, args, len, &len))
	    return FALSE;
	}
    }

  /* Get pointer to array data. */
  if (val->v_pointer == NULL)
    /* NULL array is represented by nil. */
    lua_pushnil(L);
  else
    {
      GITypeInfo* eti = g_type_info_get_param_type(ti, 0);
      GITypeTag etag = g_type_info_get_tag(eti);
      gsize size = get_type_size(etag);

      /* Create Lua table which will hold the array. */
      lua_createtable(L, len > 0 ? len : 0, 0);

      /* Iterate through array elements. */
      for (index = 0; len < 0 || index < len; index++)
	{
	  /* Get value from specified index. */
	  gint offset = index * size;
	  GArgument* eval = (GArgument*)((char*)val->v_pointer + offset);

	  /* If the array is zero-terminated, terminate now and don't
	     include NULL entry. */
	  if (len < 0 && eval->v_pointer == NULL)
	    break;

	  /* Store value into the table. */
	  if (lgi_marshal_2lua(L, eti, eval, 
			       (xfer == GI_TRANSFER_EVERYTHING) ?
			       GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			       GI_SCOPE_TYPE_INVALID, NULL, NULL))
	    lua_rawseti(L, -2, index + 1);
	}

      /* If needed, free the array itself. */
      if (xfer != GI_TRANSFER_NOTHING)
	g_free(val->v_pointer);

      /* Free element's typeinfo. */
      g_base_info_unref(eti);
    }

  return TRUE;
}

/* Marshalls single value from GLib/C to Lua.  Returns 1 if something
   was pushed to the stack. */
gboolean
lgi_marshal_2lua(lua_State* L, GITypeInfo* ti, GArgument* val,
		 GITransfer xfer, GIScopeType scope, 
		 GICallableInfo* ci, GArgument* args)
{
  gboolean own = (xfer != GI_TRANSFER_NOTHING);
  GITypeTag tag = g_type_info_get_tag(ti);
  gboolean handled = marshal_2lua_simple(L, tag, val, own);
  if (!handled)
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
		handled = 
		  marshal_2lua_simple(L, g_enum_info_get_storage_type(ii),
				      val, own);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_OBJECT:
	      case GI_INFO_TYPE_INTERFACE:
		handled = lgi_compound_create(L, ii, val->v_pointer, own);
		break;

	      default:
		g_warning("unable to marshal iface type `%d'", (int) type);
	      }
	    g_base_info_unref(ii);
	  }
	  break;

	case GI_TYPE_TAG_ARRAY:
	  {
	    GIArrayType atype = g_type_info_get_array_type(ti);
	    switch (atype)
	      {
	      case GI_ARRAY_TYPE_C:
		handled = marshal_2lua_carray(L, ti, val, xfer, ci, args);
		break;

	      default:
		g_warning("bad array type %d", atype);
	      }
	  }
	  break;
	default:
	  g_warning("unable to marshal type with tag `%d'", (int) tag);
	}
    }

  return handled;
}
