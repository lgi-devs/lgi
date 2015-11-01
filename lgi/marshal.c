/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010-2013 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implements marshalling, i.e. transferring values between Lua and GLib/C.
 */

#include <string.h>
#include <ffi.h>
#include "lgi.h"

/* Checks whether given argument contains number which fits given
   constraints. If yes, returns it, otherwise throws Lua error. */
static lua_Number
check_number (lua_State *L, int narg, lua_Number val_min, lua_Number val_max)
{
  lua_Number val = luaL_checknumber (L, narg);
  if (val < val_min || val > val_max)
    {
      lua_pushfstring (L, "%f is out of <%f, %f>", val, val_min, val_max);
      luaL_argerror (L, narg, lua_tostring (L, -1));
    }
  return val;
}

typedef union {
  GIArgument arg;
  ffi_arg u;
  ffi_sarg s;
} ReturnUnion;

/* Marshals integral types to C.  If requested, makes sure that the
   value is actually marshalled into val->v_pointer no matter what the
   input type is. */
static void
marshal_2c_int (lua_State *L, GITypeTag tag, GIArgument *val, int narg,
		gboolean optional, int parent)
{
  (void) optional;
  switch (tag)
    {
#define HANDLE_INT(nameup, namelow, ptrconv, pct, val_min, val_max, ut) \
      case GI_TYPE_TAG_ ## nameup:					\
	val->v_ ## namelow = check_number (L, narg, val_min, val_max);	\
	if (parent == LGI_PARENT_FORCE_POINTER)				\
	  val->v_pointer =						\
	    G ## ptrconv ## _TO_POINTER ((pct) val->v_ ## namelow);     \
	else if (sizeof (g ## namelow) <= sizeof (long)			\
		 && parent == LGI_PARENT_IS_RETVAL)			\
	  {								\
	    ReturnUnion *ru = (ReturnUnion *) val;			\
	    ru->ut = ru->arg.v_ ## namelow;				\
	  }								\
	break

#define HANDLE_INT_NOPTR(nameup, namelow, val_min, val_max, ut)		\
      case GI_TYPE_TAG_ ## nameup:					\
	val->v_ ## namelow = check_number (L, narg, val_min, val_max);	\
	g_assert (parent != LGI_PARENT_FORCE_POINTER);			\
	if (sizeof (g ## namelow) <= sizeof (long)			\
		 && parent == LGI_PARENT_IS_RETVAL)			\
	  {								\
	    ReturnUnion *ru = (ReturnUnion *) val;			\
	    ru->ut = ru->arg.v_ ## namelow;				\
	  }								\
	break

      HANDLE_INT(INT8, int8, INT, gint, -0x80, 0x7f, s);
      HANDLE_INT(UINT8, uint8, UINT, guint, 0, 0xff, u);
      HANDLE_INT(INT16, int16, INT, gint, -0x8000, 0x7fff, s);
      HANDLE_INT(UINT16, uint16, UINT, guint, 0, 0xffff, u);
      HANDLE_INT(INT32, int32, INT, gint, -0x80000000LL, 0x7fffffffLL, s);
      HANDLE_INT(UINT32, uint32, UINT, guint, 0, 0xffffffffUL, u);
      HANDLE_INT(UNICHAR, uint32, UINT, guint, 0, 0x7fffffffLL, u);
      HANDLE_INT_NOPTR(INT64, int64, ((lua_Number) -0x7f00000000000000LL) - 1,
		       0x7fffffffffffffffLL, s);
      HANDLE_INT_NOPTR(UINT64, uint64, 0, 0xffffffffffffffffULL, u);
#undef HANDLE_INT
#undef HANDLE_INT_NOPTR

    case GI_TYPE_TAG_GTYPE:
      {
#if GLIB_SIZEOF_SIZE_T == 4
	val->v_uint32 =
#else
	  val->v_uint64 =
#endif
	  lgi_type_get_gtype (L, narg);
      break;
      }

    default:
      g_assert_not_reached ();
    }
}

/* Marshals integral types from C to Lua. */
static void
marshal_2lua_int (lua_State *L, GITypeTag tag, GIArgument *val,
		  int parent)
{
  switch (tag)
    {
#define HANDLE_INT(nameupper, namelower, ptrconv, ut)			\
      case GI_TYPE_TAG_ ## nameupper:					\
	if (sizeof (g ## namelower) <= sizeof (long)			\
	    && parent == LGI_PARENT_IS_RETVAL)				\
	  {								\
	    ReturnUnion *ru = (ReturnUnion *) val;			\
	    ru->arg.v_ ## namelower = (g ## namelower) ru->ut;		\
	  }								\
	lua_pushnumber (L, parent == LGI_PARENT_FORCE_POINTER		\
			?  GPOINTER_TO_ ## ptrconv (val->v_pointer)	\
			: val->v_ ## namelower);			\
	break;

      HANDLE_INT(INT8, int8, INT, s);
      HANDLE_INT(UINT8, uint8, UINT, u);
      HANDLE_INT(INT16, int16, INT, s);
      HANDLE_INT(UINT16, uint16, UINT, u);
      HANDLE_INT(INT32, int32, INT, s);
      HANDLE_INT(UINT32, uint32, UINT, u);
      HANDLE_INT(UNICHAR, uint32, UINT, u);
      HANDLE_INT(INT64, int64, INT, s);
      HANDLE_INT(UINT64, uint64, UINT, u);
#undef HANDLE_INT

    case GI_TYPE_TAG_GTYPE:
      lua_pushstring (L, g_type_name (
#if GLIB_SIZEOF_SIZE_T == 4
				      val->v_uint32
#else
				      val->v_uint64
#endif
				      ));
      break;

    default:
      g_assert_not_reached ();
    }
}

/* Gets or sets the length of the array. */
static void
array_get_or_set_length (GITypeInfo *ti, gssize *get_length, gssize set_length,
			 GIBaseInfo *ci, void *args)
{
  gint param = g_type_info_get_array_length (ti);
  if (param >= 0 && ci != NULL)
    {
      GIArgument *val;
      GITypeInfo *eti;
      GIInfoType itype = g_base_info_get_type (ci);

      if (itype == GI_INFO_TYPE_FUNCTION || itype == GI_INFO_TYPE_CALLBACK)
	{
	  GIArgInfo ai;

	  if (param >= g_callable_info_get_n_args (ci))
	    return;
	  g_callable_info_load_arg (ci, param, &ai);
	  eti = g_arg_info_get_type (&ai);
	  if (g_arg_info_get_direction (&ai) == GI_DIRECTION_IN)
	    /* For input parameters, value is directly pointed to by args
	       table element. */
	    val = (GIArgument *) ((void **) args)[param];
	  else
	    /* For output arguments, args table element points to pointer
	       to value. */
	    val = *(GIArgument **) ((void **) args)[param];
	}
      else if (itype == GI_INFO_TYPE_STRUCT || itype == GI_INFO_TYPE_UNION)
	{
	  GIFieldInfo *fi;

	  if (param >= g_struct_info_get_n_fields (ci))
	    return;
	  fi = g_struct_info_get_field (ci, param);
	  eti = g_field_info_get_type (fi);
	  val = (GIArgument *) ((char *) args + g_field_info_get_offset (fi));
	  g_base_info_unref (fi);
	}
      else
	return;

      switch (g_type_info_get_tag (eti))
	{
#define HANDLE_ELT(tag, field)			\
	  case GI_TYPE_TAG_ ## tag:		\
	    if (get_length != NULL)		\
	      *get_length = val->v_ ## field;	\
	    else				\
	      val->v_ ## field = set_length;	\
	  break

	  HANDLE_ELT(INT8, int8);
	  HANDLE_ELT(UINT8, uint8);
	  HANDLE_ELT(INT16, int16);
	  HANDLE_ELT(UINT16, uint16);
	  HANDLE_ELT(INT32, int32);
	  HANDLE_ELT(UINT32, uint32);
	  HANDLE_ELT(INT64, int64);
	  HANDLE_ELT(UINT64, uint64);
#undef HANDLE_ELT

	default:
	  g_assert_not_reached ();
	}

      g_base_info_unref (eti);
    }
}

/* Retrieves pointer to GIArgument in given array, given that array
   contains elements of type ti. */
static gssize
array_get_elt_size (GITypeInfo *ti, gboolean force_ptr)
{
  gssize size = sizeof (gpointer);
  if (!g_type_info_is_pointer (ti) && !force_ptr)
    {
      switch (g_type_info_get_tag (ti))
	{
#define HANDLE_ELT(nameupper, nametype)		\
	  case GI_TYPE_TAG_ ## nameupper:	\
	    return sizeof (nametype);

	  HANDLE_ELT(BOOLEAN, gboolean);
	  HANDLE_ELT(INT8, gint8);
	  HANDLE_ELT(UINT8, guint8);
	  HANDLE_ELT(INT16, gint16);
	  HANDLE_ELT(UINT16, guint16);
	  HANDLE_ELT(INT32, gint32);
	  HANDLE_ELT(UINT32, guint32);
	  HANDLE_ELT(UNICHAR, guint32);
	  HANDLE_ELT(INT64, gint64);
	  HANDLE_ELT(UINT64, guint64);
	  HANDLE_ELT(FLOAT, gfloat);
	  HANDLE_ELT(DOUBLE, gdouble);
	  HANDLE_ELT(GTYPE, GType);
#undef HANDLE_ELT

	case GI_TYPE_TAG_INTERFACE:
	  {
	    GIBaseInfo *info = g_type_info_get_interface (ti);
	    GIInfoType type = g_base_info_get_type (info);
	    if (type == GI_INFO_TYPE_STRUCT)
	      size = g_struct_info_get_size (info);
	    else if (type == GI_INFO_TYPE_UNION)
	      size = g_union_info_get_size (info);
	    g_base_info_unref (info);
	    break;
	  }

	default:
	  break;
	}
    }

  return size;
}

static void
array_detach (GArray *array)
{
  g_array_free (array, FALSE);
}

static void
ptr_array_detach (GPtrArray *array)
{
  g_ptr_array_free (array, FALSE);
}

static void
byte_array_detach (GByteArray *array)
{
  g_byte_array_free (array, FALSE);
}

/* Marshalls array from Lua to C. Returns number of temporary elements
   pushed to the stack. */
static int
marshal_2c_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		  gpointer *out_array, gssize *out_size, int narg,
		  gboolean optional, GITransfer transfer)
{
  GITypeInfo* eti;
  gssize objlen, esize;
  gint index, vals = 0, to_pop, eti_guard;
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gboolean zero_terminated;
  GArray *array = NULL;
  int parent = 0;

  /* Represent nil as NULL array. */
  if (optional && lua_isnoneornil (L, narg))
    {
      *out_size = 0;
      *out_array = NULL;
    }
  else
    {
      /* Get element type info, create guard for it. */
      eti = g_type_info_get_param_type (ti, 0);
      lgi_gi_info_new (L, eti);
      eti_guard = lua_gettop (L);
      esize = array_get_elt_size (eti, atype == GI_ARRAY_TYPE_PTR_ARRAY);

      /* Check the type. If this is C-array of byte-sized elements, we
	 can try special-case and accept strings or buffers. */
      *out_array = NULL;
      if (lua_type (L, narg) != LUA_TTABLE && esize == 1
	  && atype == GI_ARRAY_TYPE_C)
	{
	  size_t size = 0;
	  *out_array = lgi_udata_test (L, narg, LGI_BYTES_BUFFER);
	  if (*out_array)
	    size = lua_objlen (L, narg);
	  else
	    *out_array = (gpointer *) lua_tolstring (L, narg, &size);

	  if (transfer != GI_TRANSFER_NOTHING)
	    *out_array = g_memdup (*out_array, size);

	  *out_size = size;
	}

      if (!*out_array)
	{
	  /* Otherwise, we allow only tables. */
	  luaL_checktype (L, narg, LUA_TTABLE);

	  /* Find out how long array should we allocate. */
	  zero_terminated = g_type_info_is_zero_terminated (ti);
	  objlen = lua_objlen (L, narg);
	  *out_size = g_type_info_get_array_fixed_size (ti);
	  if (atype != GI_ARRAY_TYPE_C || *out_size < 0)
	    *out_size = objlen;
	  else if (*out_size < objlen)
	    objlen = *out_size;

	  /* Allocate the array and wrap it into the userdata guard,
	     if needed. */
	  if (*out_size > 0 || zero_terminated)
	    {
	      guint total_size = *out_size + (zero_terminated ? 1 : 0);
	      switch (atype)
		{
		case GI_ARRAY_TYPE_C:
		case GI_ARRAY_TYPE_ARRAY:
		  array = g_array_sized_new (zero_terminated, TRUE, esize,
					     *out_size);
		  g_array_set_size (array, *out_size);
		  *lgi_guard_create (L, (GDestroyNotify)
				     (transfer == GI_TRANSFER_EVERYTHING
				      ? array_detach : g_array_unref)) = array;
		  break;

		case GI_ARRAY_TYPE_PTR_ARRAY:
		  parent = LGI_PARENT_FORCE_POINTER;
		  array = (GArray *) g_ptr_array_sized_new (total_size);
		  g_ptr_array_set_size ((GPtrArray *) array, total_size);
		  *lgi_guard_create (L, (GDestroyNotify)
				     (transfer == GI_TRANSFER_EVERYTHING
				      ? ptr_array_detach :
				      g_ptr_array_unref)) = array;
		  break;

		case GI_ARRAY_TYPE_BYTE_ARRAY:
		  array = (GArray *) g_byte_array_sized_new (total_size);
		  g_byte_array_set_size ((GByteArray *) array, *out_size);
		  *lgi_guard_create (L, (GDestroyNotify)
				     (transfer == GI_TRANSFER_EVERYTHING
				      ? byte_array_detach :
				      g_byte_array_unref)) = array;
		  break;
		}
	      vals = 1;
	    }

	  /* Iterate through Lua array and fill GArray accordingly. */
	  for (index = 0; index < objlen; index++)
	    {
	      lua_pushnumber (L, index + 1);
	      lua_gettable (L, narg);

	      /* Marshal element retrieved from the table into target
		 array. */
	      to_pop = lgi_marshal_2c (L, eti, NULL, exfer,
				       array->data + index * esize, -1,
				       parent, NULL, NULL);

	      /* Remove temporary element from the stack. */
	      lua_remove (L, - to_pop - 1);

	      /* Remember that some more temp elements could be
		 pushed. */
	      vals += to_pop;
	    }

	  /* Return either GArray or direct pointer to the data,
	     according to the array type. */
	  if (array == NULL)
	    *out_array = NULL;
	  else 
	    switch (atype)
	      {
	      case GI_ARRAY_TYPE_C:
		*out_array = (void *) array->data;
		break;

	      case GI_ARRAY_TYPE_ARRAY:
	      case GI_ARRAY_TYPE_PTR_ARRAY:
	      case GI_ARRAY_TYPE_BYTE_ARRAY:
		*out_array = (void *) array;
		break;
	      }
	}

      lua_remove (L, eti_guard);
    }

  return vals;
}

static void
marshal_2lua_array (lua_State *L, GITypeInfo *ti, GIDirection dir,
		    GIArrayType atype, GITransfer transfer,
		    gpointer array, gssize size, int parent)
{
  GITypeInfo *eti;
  gssize len = 0, esize;
  gint index, eti_guard;
  char *data = NULL;

  /* Avoid propagating return value marshaling flag to array elements. */
  if (parent == LGI_PARENT_IS_RETVAL)
    parent = 0;

  /* First of all, find out the length of the array. */
  if (atype == GI_ARRAY_TYPE_ARRAY)
    {
      if (array)
	{
	  len = ((GArray *) array)->len;
	  data = ((GArray *) array)->data;
	}
    }
  else if (atype == GI_ARRAY_TYPE_BYTE_ARRAY)
    {
      if (array)
	{
	  len = ((GByteArray *) array)->len;
	  data = (char *) ((GByteArray *) array)->data;
	}
    }
  else if (atype == GI_ARRAY_TYPE_PTR_ARRAY)
    {
      if (array)
	{
	  len = ((GPtrArray *) array)->len;
	  data = (char *) ((GPtrArray *) array)->pdata;
	  parent = LGI_PARENT_FORCE_POINTER;
	}
    }
  else
    {
      data = array;
      if (g_type_info_is_zero_terminated (ti))
	len = -1;
      else
	{
	  len = g_type_info_get_array_fixed_size (ti);
	  if (len == -1)
	    /* Length of the array is dynamic, get it from other
	       argument. */
	    len = size;
	}
    }

  /* Get array element type info, wrap it in the guard so that we
     don't leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  lgi_gi_info_new (L, eti);
  eti_guard = lua_gettop (L);
  esize = array_get_elt_size (eti, atype == GI_ARRAY_TYPE_PTR_ARRAY);

  /* Note that we ignore is_pointer check for uint8 type.  Although it
     is not exactly correct, we probably would not handle uint8*
     correctly anyway, this is strange type to use, and moreover this
     is workaround for g-ir-scanner bug which might mark elements of
     uint8 arrays as gconstpointer, thus setting is_pointer=true on
     it.  See https://github.com/pavouk/lgi/issues/57 */
  if (g_type_info_get_tag (eti) == GI_TYPE_TAG_UINT8)
    {
      /* UINT8 arrays are marshalled as Lua strings. */
      if (len < 0)
	len = data ? strlen(data) : 0;
      lua_pushlstring (L, data, len);
    }
  else
    {
      if (array == NULL)
	{
	  /* NULL array is represented by empty table for C arrays, nil
	     for other types. */
	  if (atype == GI_ARRAY_TYPE_C)
	    lua_newtable (L);
	  else
	    lua_pushnil (L);

	  lua_remove (L, eti_guard);
	  return;
	}

      /* Create Lua table which will hold the array. */
      lua_createtable (L, len > 0 ? len : 0, 0);

      /* Iterate through array elements. */
      for (index = 0; len < 0 || index < len; index++)
	{
	  /* Get value from specified index. */
	  GIArgument *eval = (GIArgument *) (data + index * esize);

	  /* If the array is zero-terminated, terminate now and don't
	     include NULL entry. */
	  if (len < 0 && eval->v_pointer == NULL)
	    break;

	  /* Store value into the table. */
	  lgi_marshal_2lua (L, eti, NULL, dir,
			    (transfer == GI_TRANSFER_EVERYTHING) ?
			    GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			    eval, parent, NULL, NULL);
	  lua_rawseti (L, -2, index + 1);
	}
    }

  /* If needed, free the original array. */
  if (transfer != GI_TRANSFER_NOTHING)
    {
      if (atype == GI_ARRAY_TYPE_ARRAY)
	g_array_free (array, TRUE);
      else if (atype == GI_ARRAY_TYPE_BYTE_ARRAY)
	g_byte_array_free (array, TRUE);
      else if (atype == GI_ARRAY_TYPE_PTR_ARRAY)
	g_ptr_array_free (array, TRUE);
      else
	g_free (array);
    }

  lua_remove (L, eti_guard);
}

/* Marshalls GSList or GList from Lua to C. Returns number of
   temporary elements pushed to the stack. */
static int
marshal_2c_list (lua_State *L, GITypeInfo *ti, GITypeTag list_tag,
		 gpointer *list, int narg, GITransfer transfer)
{
  GITypeInfo *eti;
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gint index, vals = 0, to_pop, eti_guard;
  GSList **guard = NULL;

  /* Allow empty list to be expressed also as 'nil', because in C,
     there is no difference between NULL and empty list. */
  if (lua_isnoneornil (L, narg))
    index = 0;
  else
    {
      luaL_checktype (L, narg, LUA_TTABLE);
      index = lua_objlen (L, narg);
    }

  /* Get list element type info, create guard for it so that we don't
     leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  lgi_gi_info_new (L, eti);
  eti_guard = lua_gettop (L);

  /* Go from back and prepend to the list, which is cheaper than
     appending. */
  guard = (GSList **) lgi_guard_create (L, list_tag == GI_TYPE_TAG_GSLIST
					? (GDestroyNotify) g_slist_free
					: (GDestroyNotify) g_list_free);
  while (index > 0)
    {
      /* Retrieve index-th element from the source table and marshall
	 it as pointer to arg. */
      GIArgument eval;
      lua_pushnumber (L, index--);
      lua_gettable (L, narg);
      to_pop = lgi_marshal_2c (L, eti, NULL, exfer, &eval, -1,
			       LGI_PARENT_FORCE_POINTER, NULL, NULL);

      /* Prepend new list element and reassign the guard. */
      if (list_tag == GI_TYPE_TAG_GSLIST)
	*guard = g_slist_prepend (*guard, eval.v_pointer);
      else
	*guard = (GSList *) g_list_prepend ((GList *) *guard, eval.v_pointer);

      lua_remove (L, - to_pop - 1);
      vals += to_pop;
    }

  /* Marshalled value is kept inside the guard. */
  *list = *guard;
  lua_remove (L, eti_guard);
  return vals;
}

static int
marshal_2lua_list (lua_State *L, GITypeInfo *ti, GIDirection dir,
		   GITypeTag list_tag, GITransfer xfer, gpointer list)
{
  GSList *i;
  GITypeInfo *eti;
  gint index, eti_guard;

  /* Get element type info, guard it so that we don't leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  lgi_gi_info_new (L, eti);
  eti_guard = lua_gettop (L);

  /* Create table to which we will deserialize the list. */
  lua_newtable (L);

  /* Go through the list and push elements into the table. */
  for (i = list, index = 0; i != NULL; i = g_slist_next (i))
    {
      /* Get access to list item. */
      GIArgument *eval = (GIArgument *) &i->data;

      /* Store it into the table. */
      lgi_marshal_2lua (L, eti, NULL, dir, (xfer == GI_TRANSFER_EVERYTHING) ?
			GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			eval, LGI_PARENT_FORCE_POINTER, NULL, NULL);
      lua_rawseti(L, -2, ++index);
    }

  /* Free the list, if we got its ownership. */
  if (xfer != GI_TRANSFER_NOTHING)
    {
      if (list_tag == GI_TYPE_TAG_GSLIST)
	g_slist_free (list);
      else
	g_list_free (list);
    }

  lua_remove (L, eti_guard);
  return 1;
}

/* Marshalls hashtable from Lua to C. Returns number of temporary
   elements pushed to the stack. */
static int
marshal_2c_hash (lua_State *L, GITypeInfo *ti, GHashTable **table, int narg,
		 gboolean optional, GITransfer transfer)
{
  GITypeInfo *eti[2];
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gint i, vals = 0, guard;
  GHashTable **guarded_table;
  GHashFunc hash_func;
  GEqualFunc equal_func;

  /* Represent nil as NULL table. */
  if (optional && lua_isnoneornil (L, narg))
    *table = NULL;
  else
    {
      /* Check the type; we allow tables only. */
      luaL_checktype (L, narg, LUA_TTABLE);

      /* Get element type infos, create guard for it. */
      guard = lua_gettop (L) + 1;
      for (i = 0; i < 2; i++)
	{
	  eti[i] = g_type_info_get_param_type (ti, i);
	  lgi_gi_info_new (L, eti[i]);
	}

      /* Create the hashtable and guard it so that it is destroyed in
	 case something goes wrong during marshalling. */
      guarded_table = (GHashTable **)
	lgi_guard_create (L, (GDestroyNotify) g_hash_table_destroy);
      vals++;

      /* Find out which hash_func and equal_func should be used,
	 according to the type of the key. */
      switch (g_type_info_get_tag (eti[0]))
	{
	case GI_TYPE_TAG_UTF8:
	case GI_TYPE_TAG_FILENAME:
	  hash_func = g_str_hash;
	  equal_func = g_str_equal;
	  break;
	case GI_TYPE_TAG_INT64:
	case GI_TYPE_TAG_UINT64:
	  hash_func = g_int64_hash;
	  equal_func = g_int64_equal;
	  break;
	case GI_TYPE_TAG_FLOAT:
	case GI_TYPE_TAG_DOUBLE:
	  return luaL_error (L, "hashtable with float or double is not "
			     "supported");
	default:
	  /* For everything else, use direct hash of stored pointer. */
	  hash_func = NULL;
	  equal_func = NULL;
	}
      *guarded_table = *table = g_hash_table_new (hash_func, equal_func);

      /* Iterate through Lua table and fill hashtable. */
      lua_pushnil (L);
      while (lua_next (L, narg))
	{
	  GIArgument eval[2];
	  int key_pos = lua_gettop (L) - 1;

	  /* Marshal key and value from the table. */
	  for (i = 0; i < 2; i++)
	    vals += lgi_marshal_2c (L, eti[i], NULL, exfer, &eval[i],
				    key_pos + i, LGI_PARENT_FORCE_POINTER,
				    NULL, NULL);

	  /* Insert newly marshalled pointers into the table. */
	  g_hash_table_insert (*table, eval[0].v_pointer, eval[1].v_pointer);

	  /* The great stack suffle; remove value completely and leave
	     key on the top of the stack.  Complicated by the fact
	     that both are burried under key_pop + val_pop elements
	     created by marshalling. */
	  lua_remove (L, key_pos + 1);
	  lua_pushvalue (L, key_pos);
	  lua_remove (L, key_pos);
	}

      /* Remove guards for element types. */
      lua_remove (L, guard);
      lua_remove (L, guard);
    }

  return vals;
}

static void
marshal_2lua_hash (lua_State *L, GITypeInfo *ti, GIDirection dir,
		   GITransfer xfer, GHashTable *hash_table)
{
  GHashTableIter iter;
  GITypeInfo *eti[2];
  gint i, guard;
  GIArgument eval[2];

  /* Check for 'NULL' table, represent it simply as nil. */
  if (hash_table == NULL)
    lua_pushnil (L);
  else
    {
      /* Get key and value type infos, guard them so that we don't
	 leak it. */
      guard = lua_gettop (L) + 1;
      for (i = 0; i < 2; i++)
	{
	  eti[i] = g_type_info_get_param_type (ti, i);
	  lgi_gi_info_new (L, eti[i]);
	}

      /* Create table to which we will deserialize the hashtable. */
      lua_newtable (L);

      /* Go through the hashtable and push elements into the table. */
      g_hash_table_iter_init (&iter, hash_table);
      while (g_hash_table_iter_next (&iter, &eval[0].v_pointer,
				     &eval[1].v_pointer))
	{
	  /* Marshal key and value to the stack. */
	  for (i = 0; i < 2; i++)
	    lgi_marshal_2lua (L, eti[i], NULL, dir, GI_TRANSFER_NOTHING,
			      &eval[i], LGI_PARENT_FORCE_POINTER, NULL, NULL);

	  /* Store these two elements to the table. */
	  lua_settable (L, -3);
	}

      /* Free the table, if requested. */
      if (xfer != GI_TRANSFER_NOTHING)
	g_hash_table_unref (hash_table);

      lua_remove (L, guard);
      lua_remove (L, guard);
    }
}

static void
marshal_2lua_error (lua_State *L, GITransfer xfer, GError *err)
{
  if (err == NULL)
    lua_pushnil (L);
  else
    {
      /* Wrap error instance with GLib.Error record. */
      lgi_type_get_repotype (L, G_TYPE_ERROR, NULL);
      lgi_record_2lua (L, err, xfer != GI_TRANSFER_NOTHING, 0);
    }
}

/* Marshalls given callable from Lua to C. */
static int
marshal_2c_callable (lua_State *L, GICallableInfo *ci, GIArgInfo *ai,
		     gpointer *callback, int narg, gboolean optional,
		     GICallableInfo *argci, void **args)
{
  int nret = 0;
  GIScopeType scope;
  gpointer user_data = NULL;
  gint nargs = 0;

  if (argci != NULL)
    nargs = g_callable_info_get_n_args (argci);

  /* Check 'nil' in optional case.  In this case, return NULL as
     callback. */
  if (lua_isnoneornil (L, narg))
    {
      if (optional)
	{
	  *callback = NULL;

	  /* Also set associated destroy handler to NULL, because some
	     callees tend to call it when left as garbage even when
	     main callback is NULL (gtk_menu_popup_for_device()
	     case). */
	  if (ai != NULL)
	    {
	      gint arg = g_arg_info_get_destroy (ai);
	      if (arg >= 0 && arg < nargs)
		((GIArgument *) args[arg])->v_pointer = NULL;
	    }
	  return 0;
	}
      else
	return luaL_argerror (L, narg, "nil is not allowed");
    }

  /* Check lightuserdata case; simply use that data if provided. */
  if (lua_islightuserdata (L, narg))
    {
      *callback = lua_touserdata (L, narg);
      return 0;
    }

  if (argci != NULL)
    {
      gint arg = g_arg_info_get_closure (ai);

      /* user_data block is already preallocated from function call. */
      g_assert (args != NULL);
      if (arg >= 0 && arg < nargs)
	{
	  user_data = ((GIArgument *) args[arg])->v_pointer;
	  arg = g_arg_info_get_destroy (ai);
	  if (arg >= 0 && arg < nargs)
	    ((GIArgument *) args[arg])->v_pointer = lgi_closure_destroy;
	}
    }

  scope = g_arg_info_get_scope (ai);
  if (user_data == NULL)
    {
      /* Closure without user_data block.  Create new data block,
	 setup destruction according to scope. */
      user_data = lgi_closure_allocate (L, 1);
      if (scope == GI_SCOPE_TYPE_CALL)
	{
	  *lgi_guard_create (L, lgi_closure_destroy) = user_data;
	  nret++;
	}
      else
	g_assert (scope == GI_SCOPE_TYPE_ASYNC);
    }

  /* Create the closure. */
  lgi_callable_create (L, ci, NULL);
  *callback = lgi_closure_create (L, user_data, narg,
				  scope == GI_SCOPE_TYPE_ASYNC);
  return nret;
}

/* Marshalls single value from Lua to GLib/C. */
int
lgi_marshal_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
		GITransfer transfer, gpointer target, int narg,
		int parent, GICallableInfo *ci, void **args)
{
  int nret = 0;
  gboolean optional = (parent == LGI_PARENT_CALLER_ALLOC) ||
    (ai == NULL || (g_arg_info_is_optional (ai) ||
		       g_arg_info_may_be_null (ai)));
  GITypeTag tag = g_type_info_get_tag (ti);
  GIArgument *arg = target;

  /* Convert narg stack position to absolute one, because during
     marshalling some temporary items might be pushed to the stack,
     which would disrupt relative stack addressing of the value. */
  lgi_makeabs(L, narg);

  switch (tag)
    {
    case GI_TYPE_TAG_BOOLEAN:
      {
	gboolean result;
	result = lua_toboolean (L, narg) ? TRUE : FALSE;
	if (parent == LGI_PARENT_FORCE_POINTER)
	  arg->v_pointer = GINT_TO_POINTER (result);
	else if (parent == LGI_PARENT_IS_RETVAL)
	  {
	    ReturnUnion *ru = (ReturnUnion *) arg;
	    ru->s = result;
	  }
	else
	  arg->v_boolean = result;
	break;
      }

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
      {
	/* Retrieve number from given position. */
	lua_Number num = (optional && lua_isnoneornil (L, narg))
	  ? 0 : luaL_checknumber (L, narg);

	/* Marshalling float/double into pointer target is not possible. */
	g_return_val_if_fail (parent != LGI_PARENT_FORCE_POINTER, 0);

	/* Store read value into chosen target. */
	if (tag == GI_TYPE_TAG_FLOAT)
	  arg->v_float = (float) num;
	else
	  arg->v_double = (double) num;
	break;
      }

    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      {
	gchar *str = NULL;
	int type = lua_type (L, narg);
	if (type == LUA_TLIGHTUSERDATA)
	  str = lua_touserdata (L, narg);
	else if (!optional || (type != LUA_TNIL && type != LUA_TNONE))
	{
	  if (type == LUA_TUSERDATA)
	    str = (gchar *) lgi_udata_test (L, narg, LGI_BYTES_BUFFER);
	  if (str == NULL)
	    str = (gchar *) luaL_checkstring (L, narg);
	}

	if (tag == GI_TYPE_TAG_FILENAME)
	  {
	    /* Convert from UTF-8 to filename encoding. */
	    if (str)
	      {
		str = g_filename_from_utf8 (str, -1, NULL, NULL, NULL);
		if (transfer != GI_TRANSFER_EVERYTHING)
		  {
		    /* Create temporary object on the stack which will
		       destroy the allocated temporary filename. */
		    *lgi_guard_create (L, g_free) = (gpointer) str;
		    nret = 1;
		  }
	      }
	  }
	else if (transfer == GI_TRANSFER_EVERYTHING)
	  str = g_strdup (str);
	if (parent == LGI_PARENT_FORCE_POINTER)
	  arg->v_pointer = str;
	else
	  arg->v_string = str;
      }
      break;

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	GIInfoType type = g_base_info_get_type (info);
	int info_guard;
	lgi_gi_info_new (L, info);
	info_guard = lua_gettop (L);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	    /* If the argument is not numeric, convert to number
	       first.  Use enum/flags 'constructor' to do this. */
	    if (lua_type (L, narg) != LUA_TNUMBER)
	      {
		lgi_type_get_repotype (L, G_TYPE_INVALID, info);
		lua_pushvalue (L, narg);
		lua_call (L, 1, 1);
		narg = -1;
	      }

	    /* Directly store underlying value. */
	    marshal_2c_int (L, g_enum_info_get_storage_type (info), arg, narg,
			    optional, parent);

	    /* Remove the temporary value, to keep stack balanced. */
	    if (narg == -1)
	      lua_pop (L, 1);
	    break;

	  case GI_INFO_TYPE_STRUCT:
	  case GI_INFO_TYPE_UNION:
	    {
	      /* Ideally the g_type_info_is_pointer() should be
		 sufficient here, but there is some
		 gobject-introspection quirk that some struct
		 arguments might not be marked as pointers
		 (e.g. g_variant_equals(), which has ctype of
		 gconstpointer, and thus logic in girparser.c which
		 sets is_pointer attribute fails).  Workaround it by
		 checking also argument type - structs as C function
		 arguments are always passed as pointers. */
	      gboolean by_value =
		parent != LGI_PARENT_FORCE_POINTER &&
		((!g_type_info_is_pointer (ti) && ai == NULL) ||
		 parent == LGI_PARENT_CALLER_ALLOC);

	      lgi_type_get_repotype (L, G_TYPE_INVALID, info);
	      lgi_record_2c (L, narg, target, by_value,
			     transfer != GI_TRANSFER_NOTHING, optional, FALSE);
	      break;
	    }

	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_INTERFACE:
	    {
	      arg->v_pointer =
		lgi_object_2c (L, narg,
			       g_registered_type_info_get_g_type (info),
			       optional, FALSE,
			       transfer != GI_TRANSFER_NOTHING);
	      break;
	    }

	  case GI_INFO_TYPE_CALLBACK:
	    nret = marshal_2c_callable (L, info, ai, &arg->v_pointer, narg,
					optional, ci, args);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
	lua_remove (L, info_guard);
      }
      break;

    case GI_TYPE_TAG_ARRAY:
      {
	gssize size;
	GIArrayType atype = g_type_info_get_array_type (ti);
	nret = marshal_2c_array (L, ti, atype, &arg->v_pointer, &size,
				 narg, optional, transfer);

	/* Fill in array length argument, if it is specified. */
	if (atype == GI_ARRAY_TYPE_C)
	  array_get_or_set_length (ti, NULL, size, ci, args);
	break;
      }

    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
      nret = marshal_2c_list (L, ti, tag, &arg->v_pointer, narg, transfer);
      break;

    case GI_TYPE_TAG_GHASH:
      nret = marshal_2c_hash (L, ti, (GHashTable **) &arg->v_pointer, narg,
			      optional, transfer);
      break;

    case GI_TYPE_TAG_VOID:
      if (g_type_info_is_pointer (ti))
	{
	  /* Check and marshal according to real Lua type. */
	  if (lua_isnoneornil (L, narg))
	    /* nil -> NULL. */
	    arg->v_pointer = NULL;
	  if (lua_type (L, narg) == LUA_TSTRING)
	    /* Use string directly. */
	    arg->v_pointer = (gpointer) lua_tostring (L, narg);
	  else
	    {
	      int type = lua_type (L, narg);
	      if (type == LUA_TLIGHTUSERDATA)
		/* Generic pointer. */
		arg->v_pointer = lua_touserdata (L, narg);
	      else
		{
		  /* Check memory buffer. */
		  arg->v_pointer = lgi_udata_test (L, narg, LGI_BYTES_BUFFER);
		  if (!arg->v_pointer)
		    {
		      /* Check object. */
		      arg->v_pointer = lgi_object_2c (L, narg, G_TYPE_INVALID,
						      FALSE, TRUE, FALSE);
		      if (!arg->v_pointer)
			{
			  /* Check any kind of record. */
			  lua_pushnil (L);
			  lgi_record_2c (L, narg, &arg->v_pointer, FALSE,
					 FALSE, FALSE, TRUE);
			}
		    }
		}
	    }
	}
      break;

    default:
      marshal_2c_int (L, tag, arg, narg, optional, parent);
    }

  return nret;
}

gboolean
lgi_marshal_2c_caller_alloc (lua_State *L, GITypeInfo *ti, GIArgument *val,
			     int pos)
{
  gboolean handled = FALSE;
  switch (g_type_info_get_tag (ti))
    {
    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *ii = g_type_info_get_interface (ti);
	GIInfoType type = g_base_info_get_type (ii);
	if (type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_UNION)
	  {
	    if (pos == 0)
	      {
		lgi_type_get_repotype (L, G_TYPE_INVALID, ii);
		val->v_pointer = lgi_record_new (L, 1, FALSE);
	      }
	    handled = TRUE;
	  }

	g_base_info_unref (ii);
	break;
      }

    case GI_TYPE_TAG_ARRAY:
      {
	if (g_type_info_get_array_type (ti) == GI_ARRAY_TYPE_C)
	  {
	    gpointer *array_guard;
	    if (pos == 0)
	      {
		gssize elt_size, size;

		/* Currently only fixed-size arrays are supported. */
		elt_size =
		  array_get_elt_size (g_type_info_get_param_type (ti, 0), FALSE);
		size = g_type_info_get_array_fixed_size (ti);
		g_assert (size > 0);

		/* Allocate underlying array.  It is temporary,
		   existing only for the duration of the call. */
		array_guard =
		  lgi_guard_create (L, (GDestroyNotify) g_array_unref);
		*array_guard = g_array_sized_new (FALSE, FALSE, elt_size, size);
		g_array_set_size (*array_guard, size);
	      }
	    else
	      {
		/* Convert the allocated array into Lua table with
		   contents. We have to do it in-place. */

		/* Make sure that pos is absolute, so that stack
		   shuffling below does not change the element it
		   points to. */
		if (pos < 0)
		  pos += lua_gettop (L) + 1;

		/* Get GArray from the guard and unmarshal it as a
		   full GArray into Lua. */
		array_guard = lua_touserdata (L, pos);
		marshal_2lua_array (L, ti, GI_DIRECTION_OUT,
				    GI_ARRAY_TYPE_ARRAY,
				    GI_TRANSFER_EVERYTHING, *array_guard,
				    -1, pos);

		/* Deactivate old guard, everything was marshalled
		   into the newly created and marshalled table. */
		*array_guard = NULL;

		/* Switch old value with the new data. */
		lua_replace (L, pos);
	      }
	    handled = TRUE;
	  }

	break;
      }

    default:
      break;
    }

  return handled;
}

/* Marshalls single value from GLib/C to Lua.  Returns 1 if something
   was pushed to the stack. */
void
lgi_marshal_2lua (lua_State *L, GITypeInfo *ti, GIArgInfo *ai, GIDirection dir,
		  GITransfer transfer, gpointer source, int parent,
		  GICallableInfo *ci, void *args)
{
  gboolean own = (transfer != GI_TRANSFER_NOTHING);
  GITypeTag tag = g_type_info_get_tag (ti);
  GIArgument *arg = source;

  /* Make sure that parent is absolute index so that it is fixed even
     when we add/remove from the stack. */
  lgi_makeabs (L, parent);

  switch (tag)
    {
    case GI_TYPE_TAG_VOID:
      if (g_type_info_is_pointer (ti))
	/* Marshal pointer to simple lightuserdata. */
	lua_pushlightuserdata (L, arg->v_pointer);
      else
	lua_pushnil (L);
      break;

    case GI_TYPE_TAG_BOOLEAN:
      if (parent == LGI_PARENT_IS_RETVAL)
	{
	  ReturnUnion *ru = (ReturnUnion *) arg;
	  ru->arg.v_boolean = ru->s;
	}
      lua_pushboolean (L, arg->v_boolean);
      break;

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
      g_return_if_fail (parent != LGI_PARENT_FORCE_POINTER);
      lua_pushnumber (L, (tag == GI_TYPE_TAG_FLOAT)
		      ? arg->v_float : arg->v_double);
      break;

    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      {
	gchar *str = (parent == LGI_PARENT_FORCE_POINTER)
	  ? arg->v_pointer : arg->v_string;
	if (tag == GI_TYPE_TAG_FILENAME && str != NULL)
	  {
	    gchar *utf8 = g_filename_to_utf8 (str, -1, NULL, NULL, NULL);
	    lua_pushstring (L, utf8);
	    g_free (utf8);
	  }
	else
	  lua_pushstring (L, str);
	if (transfer == GI_TRANSFER_EVERYTHING)
	  g_free (str);
	break;
      }

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	GIInfoType type = g_base_info_get_type (info);
	int info_guard;
	lgi_gi_info_new (L, info);
	info_guard = lua_gettop (L);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	    /* Prepare repotable of enum/flags on the stack. */
	    lgi_type_get_repotype (L, G_TYPE_INVALID, info);

	    /* Unmarshal the numeric value. */
	    marshal_2lua_int (L, g_enum_info_get_storage_type (info),
			      arg, parent);

	    /* Get symbolic value from the table. */
	    lua_gettable (L, -2);

	    /* Remove the table from the stack. */
	    lua_remove (L, -2);
	    break;

	  case GI_INFO_TYPE_STRUCT:
	  case GI_INFO_TYPE_UNION:
	    {
	      gboolean by_ref = parent == LGI_PARENT_FORCE_POINTER ||
		g_type_info_is_pointer (ti);
	      if (parent < LGI_PARENT_CALLER_ALLOC && by_ref)
		parent = 0;
	      lgi_type_get_repotype (L, G_TYPE_INVALID, info);
	      lgi_record_2lua (L, by_ref ? arg->v_pointer : source,
			       own, parent);
	      break;
	    }

	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_INTERFACE:
	    /* Avoid sinking for input arguments, because it wreaks
	       havoc to input arguments of vfunc callbacks during
	       InitiallyUnowned construction phase. */
	    lgi_object_2lua (L, arg->v_pointer, own, dir == GI_DIRECTION_IN);
	    break;

	  case GI_INFO_TYPE_CALLBACK:
	    if (arg->v_pointer == NULL)
	      lua_pushnil (L);
	    else
	      {
		lgi_callable_create (L, info, arg->v_pointer);
		if (ai != NULL && args != NULL)
		  {
		    gint closure = g_arg_info_get_closure (ai);
		    if (closure >= 0)
		      {
			/* Store context associated with the callback
			   to the callback object. */
			GIArgument *arg = ((void **) args)[closure];
			lua_pushlightuserdata (L, arg->v_pointer);
			lua_setfield (L, -2, "user_data");
		      }
		  }
	      }
	    break;

	  default:
	    g_assert_not_reached ();
	  }
	lua_remove (L, info_guard);
      }
      break;

    case GI_TYPE_TAG_ARRAY:
      {
	GIArrayType atype = g_type_info_get_array_type (ti);
	gssize size = -1;
	array_get_or_set_length (ti, &size, 0, ci, args);
	marshal_2lua_array (L, ti, dir, atype, transfer, arg->v_pointer, size,
			    parent);
      }
      break;

    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GLIST:
      marshal_2lua_list (L, ti, dir, tag, transfer, arg->v_pointer);
      break;

    case GI_TYPE_TAG_GHASH:
      marshal_2lua_hash (L, ti, dir, transfer, arg->v_pointer);
      break;

    case GI_TYPE_TAG_ERROR:
      marshal_2lua_error (L, transfer, arg->v_pointer);
      break;

    default:
      marshal_2lua_int (L, tag, arg, parent);
    }
}

int
lgi_marshal_field (lua_State *L, gpointer object, gboolean getmode,
		   int parent_arg, int field_arg, int val_arg)
{
  GITypeInfo *ti;
  int to_remove, nret;
  GIBaseInfo *pi = NULL;
  gpointer field_addr;

  /* Check the type of the field information. */
  if (lgi_udata_test (L, field_arg, LGI_GI_INFO))
    {
      GIFieldInfoFlags flags;
      GIFieldInfo **fi = lua_touserdata (L, field_arg);
      pi = g_base_info_get_container (*fi);

      /* Check, whether field is readable/writable. */
      flags = g_field_info_get_flags (*fi);
      if ((flags & (getmode ? GI_FIELD_IS_READABLE
		    : GI_FIELD_IS_WRITABLE)) == 0)
	{
	  /* Check,  whether  parent  did not disable  access  checks
	     completely. */
	  lua_getfield (L, -1, "_allow");
	  if (!lua_toboolean (L, -1))
	    {
	      /* Prepare proper error message. */
	      lua_concat (L,
			  lgi_type_get_name (L,
					     g_base_info_get_container (*fi)));
	      return luaL_error (L, "%s: field `%s' is not %s",
				 lua_tostring (L, -1),
				 g_base_info_get_name (*fi),
				 getmode ? "readable" : "writable");
	    }
	  lua_pop (L, 1);
	}

      /* Map GIArgument to proper memory location, get typeinfo of the
	 field and perform actual marshalling. */
      field_addr = (char *) object + g_field_info_get_offset (*fi);
      ti = g_field_info_get_type (*fi);
      lgi_gi_info_new (L, ti);
      to_remove = lua_gettop (L);
    }
  else
    {
      /* Consult field table, get kind of field and offset. */
      int kind;
      lgi_makeabs (L, field_arg);
      luaL_checktype (L, field_arg, LUA_TTABLE);
      lua_rawgeti (L, field_arg, 1);
      field_addr = (char *) object + lua_tointeger (L, -1);
      lua_rawgeti (L, field_arg, 2);
      kind = lua_tonumber (L, -1);
      lua_pop (L, 2);

      /* Load type information from the table and decide how to handle
	 it according to 'kind' */
      lua_rawgeti (L, field_arg, 3);
      switch (kind)
	{
	case 0:
	  /* field[3] contains typeinfo, load it and fall through. */
	  ti = *(GITypeInfo **) luaL_checkudata (L, -1, LGI_GI_INFO);
	  to_remove = lua_gettop (L);
	  break;

	case 1:
	case 2:
	  {
	    GIArgument *arg = (GIArgument *) field_addr;
	    if (getmode)
	      {
		if (kind == 1)
		  {
		    field_addr = arg->v_pointer;
		    parent_arg = 0;
		  }
		lgi_record_2lua (L, field_addr, FALSE, parent_arg);
		return 1;
	      }
	    else
	      {
		g_assert (kind == 1);
		lgi_record_2c (L, val_arg, arg->v_pointer,
			       FALSE, TRUE, FALSE, FALSE);
		return 0;
	      }
	    break;
	  }

	case 3:
	  {
	    /* Get the typeinfo for marshalling the numeric enum value. */
	    lua_rawgeti (L, field_arg, 4);
	    ti = *(GITypeInfo **) luaL_checkudata (L, -1, LGI_GI_INFO);
	    if (getmode)
	      {
		/* Use typeinfo to unmarshal numeric value. */
		lgi_marshal_2lua (L, ti, NULL, GI_DIRECTION_OUT,
				  GI_TRANSFER_NOTHING, field_addr, 0,
				  NULL, NULL);

		/* Replace numeric field with symbolic value. */
		lua_gettable (L, -3);
		lua_replace (L, -3);
		lua_pop (L, 1);
		return 1;
	      }
	    else
	      {
		/* Convert enum symbol to numeric value. */
		if (lua_type (L, val_arg != LUA_TNUMBER))
		  {
		    lua_pushvalue (L, -1);
		    lua_pushvalue (L, val_arg);
		    lua_call (L, 1, 1);
		    lua_replace (L, val_arg);
		  }

		/* Use typeinfo to marshal the numeric value. */
		lgi_marshal_2c (L, ti, NULL, GI_TRANSFER_NOTHING, field_addr,
				val_arg, 0, NULL, NULL);
		lua_pop (L, 2);
		return 0;
	      }
	  }

	default:
	  return luaL_error (L, "field has bad kind %d", kind);
	}
    }

  if (getmode)
    {
      lgi_marshal_2lua (L, ti, NULL, GI_DIRECTION_OUT, GI_TRANSFER_NOTHING,
			field_addr, parent_arg, pi, object);
      nret = 1;
    }
  else
    {
      lgi_marshal_2c (L, ti, NULL, GI_TRANSFER_EVERYTHING, field_addr, val_arg,
		      0, NULL, NULL);
      nret = 0;
    }

  lua_remove (L, to_remove);
  return nret;
}

int
lgi_marshal_access (lua_State *L, gboolean getmode,
		    int compound_arg, int element_arg, int val_arg)
{
  lua_getfield (L, -1, "_access");
  lua_pushvalue (L, -2);
  lua_pushvalue (L, compound_arg);
  lua_pushvalue (L, element_arg);
  if (getmode)
    {
      lua_call (L, 3, 1);
      return 1;
    }
  else
    {
      lua_pushvalue (L, val_arg);
      lua_call (L, 4, 0);
      return 0;
    }
}

/* Container marshaller function. */
static int
marshal_container_marshaller (lua_State *L)
{
  GValue *value;
  GITypeInfo **ti;
  GITypeTag tag;
  GITransfer transfer;
  gpointer data;
  int nret = 0;
  gboolean get_mode = lua_isnone (L, 3);

  /* Get GValue to operate on. */
  lgi_type_get_repotype (L, G_TYPE_VALUE, NULL);
  lgi_record_2c (L, 1, &value, FALSE, FALSE, FALSE, FALSE);

  /* Get raw pointer from the value. */
  if (get_mode)
    {
      if (G_VALUE_TYPE (value) == G_TYPE_POINTER)
	data = g_value_get_pointer (value);
      else
	data = g_value_get_boxed (value);
    }

  /* Get info and transfer from upvalue. */
  ti = lua_touserdata (L, lua_upvalueindex (1));
  tag = g_type_info_get_tag (*ti);
  transfer = lua_tointeger (L, lua_upvalueindex (2));

  switch (tag)
    {
    case GI_TYPE_TAG_ARRAY:
      {
	GIArrayType atype = g_type_info_get_array_type (*ti);
	gssize size = -1;
	if (get_mode)
	  {
	    if (lua_type (L, 2) == LUA_TTABLE)
	      {
		lua_getfield (L, 2, "length");
		size = luaL_optinteger (L, -1, -1);
		lua_pop (L, 1);
	      }
	    marshal_2lua_array (L, *ti, GI_DIRECTION_OUT, atype, transfer,
				data, size, 0);
	  }
	else
	  {
	    nret = marshal_2c_array (L, *ti, atype, &data, &size, 3, FALSE,
				     transfer);
	    if (lua_type (L, 2) == LUA_TTABLE)
	      {
		lua_pushnumber (L, size);
		lua_setfield (L, 2, "length");
	      }
	  }
	break;
      }

    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GLIST:
      if (get_mode)
	marshal_2lua_list (L, *ti, GI_DIRECTION_OUT, tag, transfer, data);
      else
	nret = marshal_2c_list (L, *ti, tag, &data, 3, transfer);
      break;

    case GI_TYPE_TAG_GHASH:
      if (get_mode)
	marshal_2lua_hash (L, *ti, GI_DIRECTION_OUT, transfer, data);
      else
	nret = marshal_2c_hash (L, *ti, (GHashTable **) &data, 3, FALSE,
				transfer);
      break;

    default:
      g_assert_not_reached ();
    }

  /* Store result pointer to the value. */
  if (!get_mode)
    {
      if (G_VALUE_TYPE (value) == G_TYPE_POINTER)
	g_value_set_pointer (value, data);
      else
	g_value_set_boxed (value, data);
    }

  /* If there are any temporary objects, try to store them into
     attrs.keepalive table, if it is present. */
  if (!lua_isnoneornil (L, 2))
    {
      lua_getfield (L, 2, "keepalive");
      if (!lua_isnil (L, -1))
	for (lua_insert (L, -nret - 1); nret > 0; nret--)
	  {
	    lua_pushnumber (L, lua_objlen (L, -nret - 1));
	    lua_insert (L, -2);
	    lua_settable (L, -nret - 3);
	    lua_pop (L, 1);
	  }
      else
	lua_pop (L, nret);
      lua_pop (L, 1);
    }
  else
    lua_pop (L, nret);

  return get_mode ? 1 : 0;
}

static const char* const transfers[] = { "none", "container", "full", NULL };

/* Creates container (array, list, slist, hash) marshaller for
   specified container typeinfo.  Signature is:
   marshaller = marshal.container(typeinfo, transfer) */
static int
marshal_container (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, LGI_GI_INFO);
  GITypeTag tag = g_type_info_get_tag (*info);
  GITransfer transfer = luaL_checkoption (L, 2, transfers[0], transfers);
  if (tag == GI_TYPE_TAG_ARRAY || tag == GI_TYPE_TAG_GHASH ||
      tag == GI_TYPE_TAG_GSLIST || tag == GI_TYPE_TAG_GLIST)
    {
      lua_pushvalue (L, 1);
      lua_pushnumber (L, transfer);
      lua_pushcclosure (L, marshal_container_marshaller, 2);
    }
  else
    lua_pushnil (L);
  return 1;
}

/* Fundamental marshaller closure. */
static int
marshal_fundamental_marshaller (lua_State *L)
{
  gpointer obj;
  gboolean get_mode = lua_isnone (L, 3);
  GValue *value;
  lgi_type_get_repotype (L, G_TYPE_VALUE, NULL);
  lgi_record_2c (L, 1, &value, FALSE, FALSE, FALSE, FALSE);
  if (get_mode)
    {
      /* Get fundamental from value. */
      GIObjectInfoGetValueFunction get_value =
	lua_touserdata (L, lua_upvalueindex (1));
      obj = get_value (value);
      lgi_object_2lua (L, obj, FALSE, FALSE);
      return 1;
    }
  else
    {
      /* Set fundamental to value. */
      GIObjectInfoSetValueFunction set_value =
	lua_touserdata (L, lua_upvalueindex (2));
      obj = lgi_object_2c (L, 3, G_TYPE_INVALID, FALSE, FALSE, FALSE);
      set_value (value, obj);
      return 0;
    }
}

/* Creates marshaller closure for specified fundamental object type.
   If specified object does not have custom setvalue/getvalue
   functions registered, returns nil.  Signature is:
   marshaller = marshal.fundamental(gtype) */
static int
marshal_fundamental (lua_State *L)
{
  /* Find associated baseinfo. */
  GIBaseInfo *info = g_irepository_find_by_gtype (NULL,
						  lgi_type_get_gtype (L, 1));
  if (info)
    {
      lgi_gi_info_new (L, info);
      if (GI_IS_OBJECT_INFO (info) && g_object_info_get_fundamental (info))
	{
	  GIObjectInfoGetValueFunction get_value =
	    lgi_object_get_function_ptr (info,
					 g_object_info_get_get_value_function);
	  GIObjectInfoSetValueFunction set_value =
	    lgi_object_get_function_ptr (info,
					 g_object_info_get_set_value_function);
	  if (get_value && set_value)
	    {
	      lua_pushlightuserdata (L, get_value);
	      lua_pushlightuserdata (L, set_value);
	      lua_pushcclosure (L, marshal_fundamental_marshaller, 2);
	      return 1;
	    }
	}
    }

  lua_pushnil (L);
  return 1;
}

/* Creates or marshalls content of GIArgument to/from lua according to
   specified typeinfo.
   arg, ptr = marshal.argument()
   value = marshal.argument(arg, typeinfo, transfer)
   marshal.argument(arg, typeinfo, transfer, value) */
static int
marshal_argument (lua_State *L)
{
  GITypeInfo **info;
  GITransfer transfer;
  GIArgument *arg;

  if (lua_isnone (L, 1))
    {
      /* Create new argument userdata. */
      GIArgument *arg = lua_newuserdata (L, sizeof (*arg));
      memset (arg, 0, sizeof (*arg));
      lua_pushlightuserdata (L, arg);
      return 2;
    }

  arg = lua_touserdata (L, 1);
  info = luaL_checkudata (L, 2, LGI_GI_INFO);
  transfer = luaL_checkoption (L, 3, transfers[0], transfers);
  if (lua_isnone (L, 4))
    {
      lgi_marshal_2lua (L, *info, NULL, GI_DIRECTION_IN, transfer, arg,
			0, NULL, NULL);
      return 1;
    }
  else
    {
      lua_pop (L, lgi_marshal_2c (L, *info, NULL, transfer, arg, 4,
				  0, NULL, NULL));
      return 0;
    }
}

static int
marshal_callback (lua_State *L)
{
  gpointer user_data, addr;
  GICallableInfo **ci;

  user_data = lgi_closure_allocate (L, 1);
  *lgi_guard_create (L, lgi_closure_destroy) = user_data;
  if (lua_istable (L, 1))
    lgi_callable_parse (L, 1, NULL);
  else
    {
      ci = lgi_udata_test (L, 1, LGI_GI_INFO);
      lgi_callable_create (L, *ci, NULL);
    }
  addr = lgi_closure_create (L, user_data, 2, FALSE);
  lua_pushlightuserdata (L, addr);
  return 2;
}

static void
gclosure_destroy (gpointer user_data, GClosure *closure)
{
  (void) closure;
  lgi_closure_destroy (user_data);
}

/* This is workaround for missing glib function, which should look
   like this:

   void g_closure_set_marshal_with_data (GClosure        *closure,
					 GClosureMarshal  marshal,
					 gpointer         user_data,
					 GDestroyNotify   destroy_notify);

   Such method would be introspectable.
*/
static int
marshal_closure_set_marshal (lua_State *L)
{
  GClosure *closure;
  gpointer user_data;
  GClosureMarshal marshal;
  GIBaseInfo *ci;

  ci = g_irepository_find_by_name (NULL, "GObject", "ClosureMarshal");
  lgi_type_get_repotype (L, G_TYPE_CLOSURE, NULL);
  lgi_record_2c (L, 1, &closure, FALSE, FALSE, FALSE, FALSE);
  user_data = lgi_closure_allocate (L, 1);
  lgi_callable_create (L, ci, NULL);
  marshal = lgi_closure_create (L, user_data, 2, FALSE);
  g_closure_set_marshal (closure, marshal);
  g_closure_add_invalidate_notifier (closure, user_data, gclosure_destroy);
  return 0;
}

/* Calculates size and alignment of specified type.
   size, align = marshal.typeinfo(tiinfo) */
static int
marshal_typeinfo (lua_State *L)
{
  GIBaseInfo **info = luaL_checkudata (L, 1, LGI_GI_INFO);
  switch (g_type_info_get_tag (*info))
    {
#define HANDLE_INT(upper, type)						\
      case GI_TYPE_TAG_ ## upper:					\
	{								\
	  struct Test { char offender; type examined; };		\
	  lua_pushnumber (L, sizeof (type));				\
	  lua_pushnumber (L, G_STRUCT_OFFSET (struct Test, examined));	\
	}								\
	break

      HANDLE_INT (VOID, gpointer);
      HANDLE_INT (BOOLEAN, gboolean);
      HANDLE_INT (INT8, gint8);
      HANDLE_INT (UINT8, guint8);
      HANDLE_INT (INT16, gint16);
      HANDLE_INT (UINT16, guint16);
      HANDLE_INT (INT32, gint32);
      HANDLE_INT (UINT32, guint32);
      HANDLE_INT (INT64, gint64);
      HANDLE_INT (UINT64, guint64);
      HANDLE_INT (FLOAT, gfloat);
      HANDLE_INT (DOUBLE, gdouble);
      HANDLE_INT (GTYPE, GType);
      HANDLE_INT (UTF8, const gchar *);
      HANDLE_INT (FILENAME, const gchar *);
      HANDLE_INT (UNICHAR, gunichar);

#undef HANDLE_INT

    default:
      return luaL_argerror (L, 1, "bad typeinfo");
    }

  return 2;
}

static const struct luaL_Reg marshal_api_reg[] = {
  { "container", marshal_container },
  { "fundamental", marshal_fundamental },
  { "argument", marshal_argument },
  { "callback", marshal_callback },
  { "closure_set_marshal", marshal_closure_set_marshal },
  { "typeinfo", marshal_typeinfo },
  { NULL, NULL }
};

void
lgi_marshal_init (lua_State *L)
{
  /* Create 'marshal' API table in main core API table. */
  lua_newtable (L);
  luaL_register (L, NULL, marshal_api_reg);
  lua_setfield (L, -2, "marshal");
}
