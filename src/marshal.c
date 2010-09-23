/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implements marshalling, i.e. transferring values between Lua and GLib/C.
 */

#include "lgi.h"

/* Gets or sets int value of specified parameter.  If specified
   parameter does not exist or its value cannot be converted to int,
   FALSE is returned. */
static gboolean
get_or_set_int_param (GICallableInfo *ci, GIArgument **args, int param,
		      gint *get_val, gint set_val)
{
  if (param >= 0 && param < g_callable_info_get_n_args (ci))
    {
      GIArgInfo ai;
      GITypeInfo ti;
      g_callable_info_load_arg (ci, param, &ai);
      g_arg_info_load_type (&ai, &ti);
      switch (g_type_info_get_tag (&ti))
	{
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)		\
	  case tag:                                             \
	    if (get_val != NULL)				\
	      *get_val = (int) args[param]->argf;		\
	    else						\
	      args[param]->argf = (ctype) set_val;		\
	    return TRUE;
#define DECLTYPE_NUMERIC_ONLY
#include "decltype.h"

	default:
	  break;
	}
    }

  return FALSE;
}

#define get_int_param(ci, args, param, val)		\
  get_or_set_int_param (ci, args, param, val, 0)

#define set_int_param(ci, args, param, val)		\
  get_or_set_int_param (ci, args, param, NULL, val)


/* Retrieves sizeof() specified type. */
static gsize
get_type_size (GITypeTag tag)
{
  gsize size;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)              \
      case tag:							\
	size = sizeof (ctype);					\
	break;
#include "decltype.h"

    default:
      size = sizeof (gpointer);
    }

  return size;
}

/* Marshals simple types to C.  Simple are number and strings. */
static int
marshal_2c_simple (lua_State *L, GITypeTag tag, GITransfer transfer,
		   GIArgument *val, int narg, gboolean optional)
{
  int vals = 1;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)		\
      case tag:							\
	val->argf = (optional && lua_isnoneornil (L, narg)) ?	\
	  (ctype) 0 : (ctype) check (L, narg);                  \
	if (transfer == GI_TRANSFER_EVERYTHING)                 \
	  val->argf = dup (val->argf);                          \
	break;
#include "decltype.h"

    default:
      vals = 0;
    }

  return vals;
}

#define UD_ARRAYGUARD "lgi.arrayguard"

static void
arrayguard_create (lua_State *L, GArray *array)
{
  GArray **guard;
  luaL_checkstack (L, 2, "");
  guard = lua_newuserdata (L, sizeof (GArray *));
  *guard = array;
  luaL_getmetatable (L, UD_ARRAYGUARD);
  lua_setmetatable (L, -2);
}

static int
arrayguard_gc (lua_State *L)
{
  GArray **guard = luaL_checkudata (L, 1, UD_ARRAYGUARD);
  g_array_free (*guard, TRUE);
  return 0;
}

/* Marshalls array from Lua to C. Returns number of temporary elements
   pushed to the stack. */
static int
marshal_2c_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		  GITransfer xfer, GIArgument *val, int narg,
		  gboolean optional,
		  GICallableInfo *ci, GIArgument **args)
{
  GITypeInfo* eti = g_type_info_get_param_type (ti, 0);
  GITypeTag etag = g_type_info_get_tag (eti);
  gsize size = get_type_size (etag);
  gint objlen, len, index, vals = 0, to_pop;
  GITransfer exfer = (xfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gboolean zero_terminated;
  GArray *array;

  /* Represent nil as NULL array. */
  if (optional && lua_isnoneornil (L, narg))
    {
      val->v_pointer = NULL;

      /* Fill in array length argument, if it is specified. */
      if (atype == GI_ARRAY_TYPE_C)
          set_int_param (ci, args, g_type_info_get_array_length (ti), 0);

      return 0;
    }

  /* Check the type; we allow tables, and if element type is gchar, we can
     allow also strings. */
  if ((etag != GI_TYPE_TAG_INT8 && etag != GI_TYPE_TAG_UINT8)
      || lua_type (L, narg) == LUA_TSTRING)
      luaL_checktype (L, narg, LUA_TTABLE);

  /* Find out how long array should we allocate. */
  len = g_type_info_get_array_fixed_size (ti);
  objlen = lua_objlen (L, narg);
  if (len == -1 || atype == GI_ARRAY_TYPE_ARRAY)
    len = objlen;
  else if (objlen > len && len >= 0)
    objlen = len;

  /* Allocate the array and wrap it into the userdata guard, if needed. */
  zero_terminated = g_type_info_is_zero_terminated (ti);
  if (len > 0 || zero_terminated)
    {
      array = g_array_sized_new (zero_terminated, TRUE, size, len);
      array->len = len;
      if (xfer != GI_TRANSFER_EVERYTHING)
        {
          arrayguard_create (L, array);
          vals = 1;
        }
    }

  /* Iterate through Lua array and fill GArray accordingly. */
  for (index = 0; index < objlen; index++)
    {
      lua_pushinteger (L, index + 1);
      lua_gettable (L, narg);
      to_pop = lgi_marshal_2c (L, eti, NULL, exfer,
			       (GIArgument *)(array->data + index * size), -1,
			       NULL, NULL);
      lua_remove (L, - to_pop - 1);
      vals += to_pop;
    }

  /* Fill in array length argument, if it is specified. */
  if (atype == GI_ARRAY_TYPE_C)
    set_int_param (ci, args, g_type_info_get_array_length (ti), len);

  /* Return either GArray or direct pointer to the data, according to
     the array type. */
  val->v_pointer = (atype == GI_ARRAY_TYPE_ARRAY)
    ? (void *) array : (void *) array->data;

  return vals;
}

/* Marshalls given callable from Lua to C. */
static int
marshal_2c_callable (lua_State *L, GICallableInfo *ci, GIArgInfo *ai,
		    GIArgument *val, int narg,
		    GICallableInfo *argci, GIArgument **args)
{
  int nret = 0;
  GIScopeType scope = g_arg_info_get_scope (ai);

  /* Create the closure. */
  gpointer closure = lgi_closure_create (L, ci, narg,
					 scope == GI_SCOPE_TYPE_ASYNC,
					 &val->v_pointer);

  /* Store user_data and/or destroy_notify arguments. */
  if (argci != NULL && args != NULL)
    {
      gint arg;
      gint nargs = g_callable_info_get_n_args (argci);
      arg = g_arg_info_get_closure (ai);
      if (arg >= 0 && arg < nargs)
	args[arg]->v_pointer = closure;
      arg = g_arg_info_get_destroy (ai);
      if (arg >= 0 && arg < nargs)
	args[arg]->v_pointer = lgi_closure_destroy;
    }

  /* In case of scope == SCOPE_TYPE_CALL, we have to create and store on the
     stack helper Lua userdata which destroy the closure in its gc. */
  if (scope == GI_SCOPE_TYPE_CALL)
    {
      lgi_closure_guard (L, closure);
      nret = 1;
    }

  return nret;
}

/* Marshalls single value from Lua to GLib/C. */
int
lgi_marshal_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
		GITransfer transfer, GIArgument *val, int narg,
		GICallableInfo *ci, GIArgument **args)
{
  int nret = 0;
  gboolean optional = (ai != NULL && (g_arg_info_is_optional (ai) ||
				      g_arg_info_may_be_null (ai)));
  GITypeTag tag = g_type_info_get_tag (ti);
  if (!marshal_2c_simple (L, tag, transfer, val, narg, optional))
    {
      switch (tag)
	{
	case GI_TYPE_TAG_VOID:
	  break;

	case GI_TYPE_TAG_INTERFACE:
	  {
	    GIBaseInfo* ii = g_type_info_get_interface (ti);
	    GIInfoType type = g_base_info_get_type (ii);
	    switch (type)
	      {
	      case GI_INFO_TYPE_ENUM:
	      case GI_INFO_TYPE_FLAGS:
		/* Directly store underlying value. */
		marshal_2c_simple (L, g_enum_info_get_storage_type (ii),
				   GI_TRANSFER_NOTHING, val, narg, optional);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_UNION:
	      case GI_INFO_TYPE_OBJECT:
	      case GI_INFO_TYPE_INTERFACE:
                {
                  GType gt = g_registered_type_info_get_g_type (ii);
                  nret = lgi_compound_get (L, narg, &gt, &val->v_pointer,
                                           optional);
                  break;
                }

	      case GI_INFO_TYPE_CALLBACK:
		nret = marshal_2c_callable (L, ii, ai, val, narg, ci, args);
		break;

	      default:
		g_warning ("unable to marshal2c iface type `%d'", (int) type);
	      }
	    g_base_info_unref (ii);
	  }
	  break;

	case GI_TYPE_TAG_ARRAY:
	  {
	    GIArrayType atype = g_type_info_get_array_type (ti);
	    switch (atype)
	      {
	      case GI_ARRAY_TYPE_C:
	      case GI_ARRAY_TYPE_ARRAY:
		nret = marshal_2c_array (L, ti, atype, transfer, val, narg,
					 optional, ci, args);
		break;

	      default:
		g_warning("bad array type %d", atype);
	      }
	  }
	  break;

	default:
	  g_warning("unable to marshal2c type with tag `%d'", (int) tag);
	}
    }

  return nret;
}

/* Marshals simple types to Lua.  Simple are number and
   strings. Returns TRUE if value was handled, 0 otherwise. */
static int
marshal_2lua_simple (lua_State *L, GITypeTag tag, GIArgument *val,
		     gboolean own)
{
  int vals = 1;
  switch (tag)
    {
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 valtype, valget, valset, ffitype)		\
      case tag:							\
	push (L, val->argf);					\
	if (own)						\
	  dtor (val->argf);					\
	break;
#include "decltype.h"

    default:
      vals = 0;
    }

  return vals;
}

static int
marshal_2lua_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		    GIArgument *val, GITransfer xfer,
		    GICallableInfo *ci, GIArgument **args)
{
  GITypeInfo* eti = g_type_info_get_param_type (ti, 0);
  GITypeTag etag = g_type_info_get_tag (eti);
  gsize size = get_type_size (etag);
  gint len, index;
  char *data;

  /* Get pointer to array data. */
  if (val->v_pointer == NULL)
    {
      /* NULL array is represented by nil. */
      lua_pushnil (L);
      return 1;
    }

  /* First of all, find out the length of the array. */
  if (atype == GI_ARRAY_TYPE_ARRAY)
    {
      len = ((GArray *) val->v_pointer)->len;
      data = ((GArray *) val->v_pointer)->data;
    }
  else
    {
      data = val->v_pointer;
      if (g_type_info_is_zero_terminated (ti))
	len = -1;
      else
	{
	  len = g_type_info_get_array_fixed_size (ti);
	  if (len == -1)
	    {
	      /* Length of the array is dynamic, get it from other
		 argument. */
	      if (ci == NULL)
		return 0;

	      len = g_type_info_get_array_length (ti);
	      if (!get_int_param (ci, args, len, &len))
		return 0;
	    }
	}
    }

  /* Create Lua table which will hold the array. */
  lua_createtable (L, len > 0 ? len : 0, 0);

  /* Iterate through array elements. */
  for (index = 0; len < 0 || index < len; index++)
    {
      /* Get value from specified index. */
      GIArgument *eval = (GIArgument *)(data + index * size);

      /* If the array is zero-terminated, terminate now and don't
	 include NULL entry. */
      if (len < 0 && eval->v_pointer == NULL)
	break;

      /* Store value into the table. */
      if (lgi_marshal_2lua (L, eti, eval,
			    (xfer == GI_TRANSFER_EVERYTHING) ?
			    GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			    NULL, NULL))
	lua_rawseti (L, -2, index + 1);
    }

  /* If needed, free the array itself. */
  if (xfer != GI_TRANSFER_NOTHING)
    {
      if (atype == GI_ARRAY_TYPE_ARRAY)
	g_array_free (val->v_pointer, TRUE);
      else
	g_free (val->v_pointer);
    }

  /* Free element's typeinfo. */
  g_base_info_unref (eti);
  return 1;
}

static int
marshal_2lua_list (lua_State *L, GITypeInfo *ti, GIArgument *val,
		   GITransfer xfer)
{
  GSList *list;
  GITypeInfo *eti = g_type_info_get_param_type (ti, 0);
  int index;

  /* Create table to which we will deserialize the list. */
  lua_newtable (L);

  /* Go through the list and push elements into the table. */
  for (list = (GSList *) val->v_pointer, index = 0; list != NULL;
       list = g_slist_next (list))
    {
      /* Get access to list item. */
      GIArgument *eval = (GIArgument *) &list->data;

      /* Store it into the table. */
      if (lgi_marshal_2lua (L, eti, eval,
			    (xfer == GI_TRANSFER_EVERYTHING) ?
			    GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			    NULL, NULL))
	lua_rawseti(L, -2, ++index);
    }

  /* Free the list, if requested. */
  if (xfer != GI_TRANSFER_NOTHING)
    g_slist_free (val->v_pointer);

  return 1;
}

/* Marshalls single value from GLib/C to Lua.  Returns 1 if something
   was pushed to the stack. */
int
lgi_marshal_2lua (lua_State *L, GITypeInfo *ti, GIArgument *val,
		 GITransfer xfer,
		 GICallableInfo *ci, GIArgument **args)
{
  gboolean own = (xfer != GI_TRANSFER_NOTHING);
  GITypeTag tag = g_type_info_get_tag (ti);
  int vals = marshal_2lua_simple (L, tag, val, own);
  if (vals == 0)
    {
      switch (tag)
	{
	case GI_TYPE_TAG_VOID:
	  break;

	case GI_TYPE_TAG_INTERFACE:
	  {
	    GIBaseInfo* ii = g_type_info_get_interface (ti);
	    GIInfoType type = g_base_info_get_type (ii);
	    switch (type)
	      {
	      case GI_INFO_TYPE_ENUM:
	      case GI_INFO_TYPE_FLAGS:
		/* Directly store underlying value. */
		vals =
		  marshal_2lua_simple (L, g_enum_info_get_storage_type (ii),
				       val, own);
		break;

	      case GI_INFO_TYPE_STRUCT:
	      case GI_INFO_TYPE_UNION:
	      case GI_INFO_TYPE_OBJECT:
	      case GI_INFO_TYPE_INTERFACE:
		vals = lgi_compound_create (L, ii, val->v_pointer, own);
		break;

	      default:
		g_warning ("unable to marshal2lua iface type `%d'", (int) type);
	      }
	    g_base_info_unref (ii);
	  }
	  break;

	case GI_TYPE_TAG_ARRAY:
	  {
	    GIArrayType atype = g_type_info_get_array_type (ti);
	    switch (atype)
	      {
	      case GI_ARRAY_TYPE_C:
	      case GI_ARRAY_TYPE_ARRAY:
		vals = marshal_2lua_array (L, ti, atype, val, xfer, ci, args);
		break;

	      default:
		g_warning ("bad array type %d", atype);
	      }
	  }
	  break;

	case GI_TYPE_TAG_GSLIST:
	case GI_TYPE_TAG_GLIST:
	  vals = marshal_2lua_list (L, ti, val, xfer);
	  break;

	default:
	  g_warning ("unable to marshal2lua type with tag `%d'", (int) tag);
	}
    }

  return vals;
}

void
lgi_marshal_init (lua_State *L)
{
  /* Register guards metatables. */
  luaL_newmetatable (L, UD_ARRAYGUARD);
  lua_pushcfunction (L, arrayguard_gc);
  lua_setfield (L, -2, "__gc");
  lua_pop (L, 1);
}
