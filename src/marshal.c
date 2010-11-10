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

GType
lgi_get_gtype (lua_State *L, GITypeInfo *ti)
{
  GType gtype = G_TYPE_INVALID;
  switch (g_type_info_get_tag (ti))
    {
#define HANDLE_TAG(tag, gt)			\
    case GI_TYPE_TAG_ ## tag:			\
      return G_TYPE_ ## gt

      HANDLE_TAG (BOOLEAN, BOOLEAN);
      HANDLE_TAG (INT8, CHAR);
      HANDLE_TAG (UINT8, UCHAR);
      HANDLE_TAG (INT16, INT);
      HANDLE_TAG (UINT16, UINT);
      HANDLE_TAG (INT32, INT);
      HANDLE_TAG (UINT32, UINT);
      HANDLE_TAG (INT64, INT64);
      HANDLE_TAG (UINT64, UINT64);
      HANDLE_TAG (FLOAT, FLOAT);
      HANDLE_TAG (DOUBLE, DOUBLE);
      HANDLE_TAG (GTYPE, GTYPE);
      HANDLE_TAG (UTF8, STRING);
      HANDLE_TAG (FILENAME, STRING);
      HANDLE_TAG (GHASH, HASH_TABLE);
      HANDLE_TAG (GLIST, POINTER);
      HANDLE_TAG (GSLIST, POINTER);
      HANDLE_TAG (ERROR, ERROR);

#undef HANDLE_TAG

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	g_assert (GI_IS_REGISTERED_TYPE_INFO (info));
	gtype = g_registered_type_info_get_g_type (info);
	g_base_info_unref (info);
	break;
      }

    default:
      g_assert_not_reached ();
    }

  return gtype;
}

/* Checks whether given argument contains number which fits given
   constraints. If yes, returns it, otehrwise throws Lua error. */
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

/* Marshals integral types to C.  If requested, makes sure that the
   value is actually marshalled into val->v_pointer no matter what the
   input type is. */
static void
marshal_2c_int (lua_State *L, GITypeTag tag, GIArgument *val, int narg,
		gboolean optional, gboolean use_pointer)
{
  switch (tag)
    {
#define HANDLE_INT(nameup, namelow, ptrconv, pct, val_min, val_max)     \
      case GI_TYPE_TAG_ ## nameup:					\
	val->v_ ## namelow = check_number (L, narg, val_min, val_max);	\
	if (use_pointer)                                                \
	  val->v_pointer =						\
	    G ## ptrconv ## _TO_POINTER ((pct) val->v_ ## namelow);     \
	break

#define HANDLE_INT_NOPTR(nameup, namelow, val_min, val_max)             \
      case GI_TYPE_TAG_ ## nameup:					\
	val->v_ ## namelow = check_number (L, narg, val_min, val_max);	\
	g_assert (!use_pointer);                                        \
	break

      HANDLE_INT(INT8, int8, INT, gint, -0x80, 0x7f);
      HANDLE_INT(UINT8, uint8, UINT, guint, 0, 0xff);
      HANDLE_INT(INT16, int16, INT, gint, -0x8000, 0x7fff);
      HANDLE_INT(UINT16, uint16, UINT, guint, 0, 0xffff);
      HANDLE_INT(INT32, int32, INT, gint, -0x80000000LL, 0x7fffffffLL);
      HANDLE_INT(UINT32, uint32, UINT, guint, 0, 0xffffffffUL);
      HANDLE_INT_NOPTR(INT64, int64, ((lua_Number) -0x7f00000000000000LL) - 1,
		       0x7fffffffffffffffLL);
      HANDLE_INT_NOPTR(UINT64, uint64, 0, 0xffffffffffffffffULL);
#if GLIB_SIZEOF_SIZE_T == 4
      HANDLE_INT_NOPTR(GTYPE, uint32, 0, 0xffffffffUL);
#else
      HANDLE_INT_NOPTR(GTYPE, uint64, 0, 0xffffffffffffffffULL);
#endif
#undef HANDLE_INT
#undef HANDLE_INT_NOPTR

    default:
      g_assert_not_reached ();
    }
}

/* Marshals integral types from C to Lua. */
static void
marshal_2lua_int (lua_State *L, GITypeTag tag, GIArgument *val,
		  gboolean use_pointer)
{
  switch (tag)
    {
#define HANDLE_INT(nameupper, namelower, ptrconv)	\
      case GI_TYPE_TAG_ ## nameupper:			\
	lua_pushnumber (L, use_pointer			\
	  ?  GPOINTER_TO_ ## ptrconv (val->v_pointer)	\
	  : val->v_ ## namelower);			\
	break;

      HANDLE_INT(INT8, int8, INT);
      HANDLE_INT(UINT8, uint8, UINT);
      HANDLE_INT(INT16, int16, INT);
      HANDLE_INT(UINT16, uint16, UINT);
      HANDLE_INT(INT32, int32, INT);
      HANDLE_INT(UINT32, uint32, UINT);
      HANDLE_INT(INT64, int64, INT);
      HANDLE_INT(UINT64, uint64, UINT);
#if GLIB_SIZEOF_SIZE_T == 4
      HANDLE_INT(GTYPE, uint32, UINT);
#else
      HANDLE_INT(GTYPE, uint64, UINT);
#endif
#undef HANDLE_INT

    default:
      g_assert_not_reached ();
    }
}

/* Gets or sets the length of the array. */
static void
array_get_or_set_length (GITypeInfo *ti, gssize *get_length, gssize set_length,
			 GICallableInfo *ci, void **args)
{
  gint param = g_type_info_get_array_length (ti);
  g_assert (ci != NULL);
  if (param >= 0 && param < g_callable_info_get_n_args (ci))
    {
      GIArgInfo ai;
      GITypeInfo eti;
      GIArgument *val;
      g_callable_info_load_arg (ci, param, &ai);
      g_arg_info_load_type (&ai, &eti);
      if (g_arg_info_get_direction (&ai) == GI_DIRECTION_IN)
	/* For input parameters, value is directly pointed do by args
	   table element. */
	val = (GIArgument *) args[param];
      else
	/* For output arguments, args table element points to pointer
	   to value. */
	val = *(GIArgument **) args[param];

      switch (g_type_info_get_tag (&eti))
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
    }
}

/* Retrieves pointer to GIArgument in given array, given that array
   contains elements of type ti. */
static gssize
array_get_elt_size (GITypeInfo *ti)
{
  gssize size = sizeof (gpointer);
  switch (g_type_info_get_tag (ti))
    {
#define HANDLE_ELT(nameupper, nametype)		\
      case GI_TYPE_TAG_ ## nameupper:		\
	return sizeof (nametype);

      HANDLE_ELT(BOOLEAN, gboolean);
      HANDLE_ELT(INT8, gint8);
      HANDLE_ELT(UINT8, guint8);
      HANDLE_ELT(INT16, gint16);
      HANDLE_ELT(UINT16, guint16);
      HANDLE_ELT(INT32, gint32);
      HANDLE_ELT(UINT32, guint32);
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

  return size;
}

/* Marshalls array from Lua to C. Returns number of temporary elements
   pushed to the stack. */
static int
marshal_2c_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		  GIArgument *val, int narg, gboolean optional,
		  GITransfer transfer, GICallableInfo *ci, void **args)
{
  GITypeInfo* eti;
  gssize len, objlen, esize;
  gint index, vals = 0, to_pop, eti_guard;
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gboolean zero_terminated;
  GArray *array = NULL;

  /* Represent nil as NULL array. */
  if (optional && lua_isnoneornil (L, narg))
    {
      len = 0;
      val->v_pointer = NULL;
    }
  else
    {
      /* Check the type; we allow tables only. */
      luaL_checktype (L, narg, LUA_TTABLE);

      /* Get element type info, create guard for it. */
      eti = g_type_info_get_param_type (ti, 0);
      eti_guard = lgi_guard_create_baseinfo (L, eti);
      esize = array_get_elt_size (eti);

      /* Find out how long array should we allocate. */
      zero_terminated = g_type_info_is_zero_terminated (ti);
      objlen = lua_objlen (L, narg);
      len = g_type_info_get_array_fixed_size (ti);
      if (atype != GI_ARRAY_TYPE_C || len < 0)
	len = objlen;
      else if (len < objlen)
	objlen = len;

      /* Allocate the array and wrap it into the userdata guard, if needed. */
      if (len > 0 || zero_terminated)
	{
	  GArray **guard;
	  array = g_array_sized_new (zero_terminated, TRUE, esize, len);
	  g_array_set_size (array, len);
	  lgi_guard_create (L, (gpointer **) &guard,
			    (GDestroyNotify) g_array_unref);
	  *guard = array;
	  vals = 1;
	}

      /* Iterate through Lua array and fill GArray accordingly. */
      for (index = 0; index < objlen; index++)
	{
	  lua_pushinteger (L, index + 1);
	  lua_gettable (L, narg);

	  /* Marshal element retrieved from the table into target array. */
	  to_pop = lgi_marshal_arg_2c (L, eti, NULL, exfer,
				       (GIArgument *) (array->data +
						       index * esize),
				       -1, FALSE, NULL, NULL);

	  /* Remove temporary element from the stack. */
	  lua_remove (L, - to_pop - 1);

	  /* Remember that some more temp elements could be pushed. */
	  vals += to_pop;
	}

      /* Return either GArray or direct pointer to the data, according to the
	 array type. */
      val->v_pointer = (atype == GI_ARRAY_TYPE_ARRAY || array == NULL)
	  ? (void *) array : (void *) array->data;

      lua_remove (L, eti_guard);
    }

  /* Fill in array length argument, if it is specified. */
  if (atype == GI_ARRAY_TYPE_C)
    array_get_or_set_length (ti, NULL, len, ci, args);

  return vals;
}

static void
marshal_2lua_array (lua_State *L, GITypeInfo *ti, GIArrayType atype,
		    GITransfer transfer, GIArgument *val, int parent,
		    GICallableInfo *ci, void **args)
{
  GITypeInfo *eti;
  gssize len, esize;
  gint index, eti_guard;
  char *data;

  /* Get pointer to array data. */
  if (val->v_pointer == NULL)
    {
      /* NULL array is represented by nil. */
      lua_pushnil (L);
      return;
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
	    /* Length of the array is dynamic, get it from other
	       argument. */
	    array_get_or_set_length (ti, &len, 0, ci, args);
	}
    }

  /* Get array element type info, wrap it in the guard so that we
     don't leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  eti_guard = lgi_guard_create_baseinfo (L, eti);
  esize = array_get_elt_size (eti);

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
      lgi_marshal_arg_2lua (L, eti, (transfer == GI_TRANSFER_EVERYTHING) ?
			    GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			    eval, parent, FALSE, NULL, NULL);
      lua_rawseti (L, -2, index + 1);
    }

  /* If needed, free the array itself. */
  if (transfer != GI_TRANSFER_NOTHING)
    {
      if (atype == GI_ARRAY_TYPE_ARRAY)
	g_array_free (val->v_pointer, TRUE);
      else
	g_free (val->v_pointer);
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
  GITypeTag etag;
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
  eti_guard = lgi_guard_create_baseinfo (L, eti);
  etag = g_type_info_get_tag (eti);

  /* Go from back and prepend to the list, which is cheaper than
     appending. */
  lgi_guard_create (L, (gpointer **) &guard,
		    list_tag == GI_TYPE_TAG_GSLIST
		    ? (GDestroyNotify) g_slist_free
		    : (GDestroyNotify) g_list_free);
  while (index > 0)
    {
      /* Retrieve index-th element from the source table and marshall
	 it as pointer to arg. */
      GIArgument eval;
      lua_pushinteger (L, index--);
      lua_gettable (L, narg);
      to_pop = lgi_marshal_arg_2c (L, eti, NULL, exfer, &eval, -1, TRUE,
				   NULL, NULL);

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
marshal_2lua_list (lua_State *L, GITypeInfo *ti, GITypeTag list_tag,
		   GITransfer xfer, gpointer list)
{
  GSList *i;
  GITypeInfo *eti;
  gint index, eti_guard;

  /* Get element type info, guard it so that we don't leak it. */
  eti = g_type_info_get_param_type (ti, 0);
  eti_guard = lgi_guard_create_baseinfo (L, eti);

  /* Create table to which we will deserialize the list. */
  lua_newtable (L);

  /* Go through the list and push elements into the table. */
  for (i = list, index = 0; i != NULL; i = g_slist_next (i))
    {
      /* Get access to list item. */
      GIArgument *eval = (GIArgument *) &i->data;

      /* Store it into the table. */
      lgi_marshal_arg_2lua (L, eti, (xfer == GI_TRANSFER_EVERYTHING) ?
			    GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING,
			    eval, 0, TRUE, NULL, NULL);
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

/* Marshalls array from Lua to C. Returns number of temporary elements
   pushed to the stack. */
static int
marshal_2c_hash (lua_State *L, GITypeInfo *ti, GHashTable **table, int narg,
		 gboolean optional, GITransfer transfer)
{
  GITypeInfo *eti[2];
  GITransfer exfer = (transfer == GI_TRANSFER_EVERYTHING
		      ? GI_TRANSFER_EVERYTHING : GI_TRANSFER_NOTHING);
  gint i, vals = 0, guard, table_guard;
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
	  lgi_guard_create_baseinfo (L, eti[i]);
	}

      /* Create the hashtable and guard it so that it is destroyed in
	 case something goes wrong during marshalling. */
      table_guard = lgi_guard_create (L, (gpointer **) &guarded_table,
				      (GDestroyNotify) g_hash_table_destroy);
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
	  luaL_error (L, "hashtable with float or double is not supported");
	  break;
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
	    vals += lgi_marshal_arg_2c (L, eti[i], NULL, exfer, &eval[i],
					key_pos + i, TRUE, NULL, NULL);

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
marshal_2lua_hash (lua_State *L, GITypeInfo *ti, GITransfer xfer,
		   GHashTable *hash_table)
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
	  lgi_guard_create_baseinfo (L, eti[i]);
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
	    lgi_marshal_arg_2lua (L, eti[i], GI_TRANSFER_NOTHING, &eval[i],
				  0, TRUE, NULL, NULL);

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

/* Marshalls given callable from Lua to C. */
static int
marshal_2c_callable (lua_State *L, GICallableInfo *ci, GIArgInfo *ai,
		    GIArgument *val, int narg,
		    GICallableInfo *argci, void **args)
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
	((GIArgument *) args[arg])->v_pointer = closure;
      arg = g_arg_info_get_destroy (ai);
      if (arg >= 0 && arg < nargs)
	((GIArgument *) args[arg])->v_pointer = lgi_closure_destroy;
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
lgi_marshal_arg_2c (lua_State *L, GITypeInfo *ti, GIArgInfo *ai,
		    GITransfer transfer, GIArgument *val, int narg,
		    gboolean use_pointer, GICallableInfo *ci, void **args)
{
  int nret = 0;
  gboolean optional = (ai != NULL && (g_arg_info_is_optional (ai) ||
				      g_arg_info_may_be_null (ai)));
  GITypeTag tag = g_type_info_get_tag (ti);

  /* Convert narg stack position to absolute one, because during
     marshalling some temporary items might be pushed to the stack,
     which would disrupt relative stack addressing of the value. */
  if (narg < 0)
    narg += lua_gettop (L) + 1;

  switch (tag)
    {
    case GI_TYPE_TAG_BOOLEAN:
      if (!optional && lua_isnoneornil (L, narg))
	luaL_typerror (L, narg, lua_typename (L, LUA_TBOOLEAN));
      val->v_boolean = lua_toboolean (L, narg) ? TRUE : FALSE;
      break;

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
      {
	/* Retrieve number from given position. */
	lua_Number num = (optional && lua_isnoneornil (L, narg))
	  ? 0 : luaL_checknumber (L, narg);

	/* Decide where to store the number. */
	GIArgument *target;
	if (!use_pointer)
	  /* Marshal directly into val. */
	  target = val;
	else
	  {
	    /* Create temporary (inside userdata), and marshal to it
	       instead of using 'val' directly. */
	    target = val->v_pointer = lua_newuserdata (L, sizeof (GIArgument));
	    nret = 1;
	  }

	/* Store read value into chosen target. */
	if (tag == GI_TYPE_TAG_FLOAT)
	  target->v_float = (float) num;
	else
	  target->v_double = (double) num;
	break;
      }

      /* We have no distinction between filename and utf8, Lua does
	 not enforce any encoding on the strings. */
    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      {
	const gchar *str = (optional && lua_isnoneornil (L, narg)
			    ? NULL : luaL_checkstring (L, narg));
	if (transfer == GI_TRANSFER_EVERYTHING)
	  str = g_strdup (str);
	if (use_pointer)
	  val->v_pointer = (gchar *)str;
	else
	  val->v_string = (gchar *)str;
      }
      break;

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	int info_guard = lgi_guard_create_baseinfo (L, info);
	GIInfoType type = g_base_info_get_type (info);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	    /* Directly store underlying value. */
	    marshal_2c_int (L, g_enum_info_get_storage_type (info), val, narg,
			    optional, FALSE);
	    break;

	  case GI_INFO_TYPE_STRUCT:
	  case GI_INFO_TYPE_UNION:
	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_INTERFACE:
	    {
	      GType gtype = g_registered_type_info_get_g_type (info);
	      nret = lgi_compound_get (L, narg, &gtype, &val->v_pointer,
				       optional ? LGI_FLAGS_OPTIONAL : 0);
	      break;
	    }

	  case GI_INFO_TYPE_CALLBACK:
	    nret = marshal_2c_callable (L, info, ai, val, narg, ci, args);
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
	switch (atype)
	  {
	  case GI_ARRAY_TYPE_C:
	  case GI_ARRAY_TYPE_ARRAY:
	    nret = marshal_2c_array (L, ti, atype, val, narg, optional,
				     transfer, ci, args);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
	break;
      }

    case GI_TYPE_TAG_GLIST:
    case GI_TYPE_TAG_GSLIST:
      nret = marshal_2c_list (L, ti, tag, &val->v_pointer, narg, transfer);
      break;

    case GI_TYPE_TAG_GHASH:
      nret = marshal_2c_hash (L, ti, (GHashTable **) &val->v_pointer, narg,
			      optional, transfer);
      break;

    default:
      marshal_2c_int (L, tag, val, narg, optional, use_pointer);
    }

  return nret;
}

void
lgi_marshal_val_2c (lua_State *L, GITypeInfo *ti, GITransfer xfer,
		    GValue *val, int narg)
{
  /* Initial decision is up to GValue type; if the type reported by
     GValue identifies type uniquelly, we ignore 'ti' arg
     completely. */
  int vals;
  gpointer obj;
  GType type = G_VALUE_TYPE (val);
  if (!G_TYPE_IS_VALUE (type))
    return;
#define HANDLE_GTYPE(gtype, getter, setter)		\
  else if (type == G_TYPE_ ## gtype)			\
    {							\
      g_value_ ## setter (val, getter (L, narg));	\
      return;						\
    }

  HANDLE_GTYPE(BOOLEAN, lua_toboolean, set_boolean)
  HANDLE_GTYPE(CHAR, luaL_checkinteger, set_char)
  HANDLE_GTYPE(UCHAR, luaL_checkinteger, set_uchar)
  HANDLE_GTYPE(INT, luaL_checkinteger, set_int)
  HANDLE_GTYPE(UINT, luaL_checknumber, set_uint)
  HANDLE_GTYPE(LONG, luaL_checknumber, set_long)
  HANDLE_GTYPE(ULONG, luaL_checknumber, set_ulong)
  HANDLE_GTYPE(INT64, luaL_checknumber, set_int64)
  HANDLE_GTYPE(UINT64, luaL_checknumber, set_uint64)
  HANDLE_GTYPE(FLOAT, luaL_checknumber, set_float)
  HANDLE_GTYPE(DOUBLE, luaL_checknumber, set_double)
  HANDLE_GTYPE(GTYPE, luaL_checknumber, set_gtype)
  HANDLE_GTYPE(STRING, luaL_checkstring, set_string)
  HANDLE_GTYPE(ENUM, luaL_checkinteger, set_enum)
  HANDLE_GTYPE(FLAGS, luaL_checkinteger, set_flags)
#undef HANDLE_GTYPE

  /* If we have typeinfo, try to use it for some specific cases. */
  if (ti != NULL)
    {
      GITypeTag tag = g_type_info_get_tag (ti);
      switch (tag)
	{
	case GI_TYPE_TAG_GHASH:
	  {
	    GHashTable *table;
	    marshal_2c_hash (L, ti, &table, narg, FALSE, GI_TRANSFER_NOTHING);
	    g_value_set_boxed (val, table);
	    return;
	  }

	case GI_TYPE_TAG_GLIST:
	case GI_TYPE_TAG_GSLIST:
	  {
	    gpointer list;
	    marshal_2c_list (L, ti, tag, &list, narg, GI_TRANSFER_NOTHING);
	    g_value_set_pointer (val, list);
	    return;
	  }

	default:
	  break;
	}
    }

  /* Handle other cases. */
  switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_ENUM:
      {
	g_value_set_enum (val, luaL_checkinteger (L, narg));
	return;
      }

    case G_TYPE_FLAGS:
      {
	g_value_set_flags (val, luaL_checkinteger (L, narg));
	return;
      }

    case G_TYPE_BOXED:
      {
	vals = lgi_compound_get (L, narg, &type, &obj, LGI_FLAGS_OPTIONAL);
	g_value_set_boxed (val, obj);
	lua_pop (L, vals);
	return;
      }

    case G_TYPE_OBJECT:
      {
	vals = lgi_compound_get (L, narg, &type, &obj, LGI_FLAGS_OPTIONAL);
	g_value_set_object (val, obj);
	lua_pop (L, vals);
	return;
      }

    default:
      {
	/* Try fundamentals; they have custom gtype. */
	GIBaseInfo *info = g_irepository_find_by_gtype (NULL, type);
	if (info != NULL)
	  {
	    GIObjectInfoSetValueFunction set_value = NULL;
	    if (g_base_info_get_type (info) == GI_INFO_TYPE_OBJECT &&
		g_object_info_get_fundamental (info))
	      set_value = g_object_info_get_set_value_function_pointer (info);
	    g_base_info_unref (info);
	    if (set_value != NULL)
	      {
		gpointer obj;
		vals = lgi_compound_get (L, narg, &type, &obj,
					 LGI_FLAGS_OPTIONAL);
		set_value (val, obj);
		lua_pop (L, vals);
		return;
	      }
	  }
      }
    }

  luaL_error (L, "g_value_set: no handling of %s(%s)",
	      g_type_name (type), g_type_name (G_TYPE_FUNDAMENTAL (type)));
}

gboolean
lgi_marshal_arg_2c_caller_alloc (lua_State *L, GITypeInfo *ti, GIArgument *val,
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
	      val->v_pointer = lgi_compound_struct_new (L, ii);
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
		  array_get_elt_size (g_type_info_get_param_type (ti, 0));
		size = g_type_info_get_array_fixed_size (ti);
		g_assert (size > 0);

		/* Allocate underlying array.  It is temporary,
		   existing only for the duration of the call. */
		lgi_guard_create (L, &array_guard,
				  (GDestroyNotify) g_array_unref);
		*array_guard = g_array_sized_new (FALSE, FALSE, elt_size,
						  size);
		g_array_set_size (*array_guard, size);
	      }
	    else
	      {
		/* Convert the allocated array into Lua table with
		   contents. We have to do it in-place. */
		GIArgument array_arg;

		/* Make sure that pos is absolute, so that stack
		   shuffling below does not change the elemnt it
		   points to. */
		if (pos < 0)
		  pos += lua_gettop (L) + 1;

		/* Get GArray from the guard and unmarshal it as a
		   full GArray into Lua. */
		lgi_guard_get_data (L, pos,
		&array_guard); array_arg.v_pointer = *array_guard;
		marshal_2lua_array (L, ti, GI_ARRAY_TYPE_ARRAY,
		GI_TRANSFER_EVERYTHING, &array_arg, pos, NULL, NULL);

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
lgi_marshal_arg_2lua (lua_State *L, GITypeInfo *ti, GITransfer transfer,
		      GIArgument *val, int parent, gboolean use_pointer,
		      GICallableInfo *ci, void **args)
{
  gboolean own = (transfer != GI_TRANSFER_NOTHING);
  GITypeTag tag = g_type_info_get_tag (ti);

  /* Make sure that parent is absolute index so that it is fixed even
     when we add/remove from the stack. */
  if (parent < 0)
    parent += lua_gettop (L) + 1;

  switch (tag)
    {
    case GI_TYPE_TAG_BOOLEAN:
      lua_pushboolean (L, val->v_boolean);
      break;

    case GI_TYPE_TAG_FLOAT:
    case GI_TYPE_TAG_DOUBLE:
      {
	/* Decide from where to load the number. */
	GIArgument *source;
	if (!use_pointer)
	  /* Marshal directly from val. */
	  source = val;
	else
	  /* Marshal from argument pointed to by value. */
	  source = (GIArgument *) val->v_pointer;

	/* Store read value into chosen source. */
	lua_pushnumber (L, (tag == GI_TYPE_TAG_FLOAT)
			? source->v_float : source->v_double);
	break;
      }

    case GI_TYPE_TAG_UTF8:
    case GI_TYPE_TAG_FILENAME:
      {
	gchar *str = use_pointer ? val->v_pointer : val->v_string;
	lua_pushstring (L, str);
	if (transfer == GI_TRANSFER_EVERYTHING)
	  g_free (str);
	break;
      }

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo *info = g_type_info_get_interface (ti);
	int info_guard = lgi_guard_create_baseinfo (L, info);
	GIInfoType type = g_base_info_get_type (info);
	switch (type)
	  {
	  case GI_INFO_TYPE_ENUM:
	  case GI_INFO_TYPE_FLAGS:
	    /* Directly store underlying value. */
	    marshal_2lua_int (L, g_enum_info_get_storage_type (info),
			      val, FALSE);
	    break;

	  case GI_INFO_TYPE_STRUCT:
	  case GI_INFO_TYPE_UNION:
	  case GI_INFO_TYPE_OBJECT:
	  case GI_INFO_TYPE_INTERFACE:
	    {
	      gpointer addr = val->v_pointer;
	      if ((type == GI_INFO_TYPE_STRUCT || type == GI_INFO_TYPE_UNION)
		  && parent != 0)
		/* If struct or union allocated inside parent, the
		   address is actually address of argument itself, not
		   the pointer inside. */
		addr = val;

	      /* If we do not own the compound, we should try to take
		 its ownership; keeping pointer to compound without
		 owning it is dangerous, it might be destroyed under
		 our hands. */
	      if (!own && (type == GI_INFO_TYPE_OBJECT
			   || type == GI_INFO_TYPE_INTERFACE))
		{
		  if (!g_object_info_get_fundamental (info))
		    {
		      /* Standard GObject, use standard method. */
		      g_object_ref (addr);
		      own = TRUE;
		    }
		  else
		    {
		      /* Try to use custom ref method. */
		      GIObjectInfoRefFunction ref =
			g_object_info_get_ref_function_pointer (info);
		      if (ref != NULL)
			{
			  ref (addr);
			  own = TRUE;
			}
		    }
		}

	      lgi_compound_create (L, info, addr, own, parent);
	      break;
	    }

	  default:
	    g_assert_not_reached ();
	  }
	lua_remove (L, info_guard);
      }
      break;

    case GI_TYPE_TAG_ARRAY:
      {
	GIArrayType atype = g_type_info_get_array_type (ti);
	switch (atype)
	  {
	  case GI_ARRAY_TYPE_C:
	  case GI_ARRAY_TYPE_ARRAY:
	    marshal_2lua_array (L, ti, atype, transfer, val, parent, ci, args);
	    break;

	  default:
	    g_assert_not_reached ();
	  }
      }
      break;

    case GI_TYPE_TAG_GSLIST:
    case GI_TYPE_TAG_GLIST:
      marshal_2lua_list (L, ti, tag, transfer, val->v_pointer);
      break;

    case GI_TYPE_TAG_GHASH:
      marshal_2lua_hash (L, ti, transfer, val->v_pointer);
      break;

    default:
      marshal_2lua_int (L, tag, val, use_pointer);
    }
}

void
lgi_marshal_val_2lua (lua_State *L, GITypeInfo *ti, GITransfer xfer,
		      const GValue *val)
{
  GType type = G_VALUE_TYPE (val);
  if (!G_TYPE_IS_VALUE (type))
    {
      lua_pushnil (L);
      return;
    }
#define HANDLE_GTYPE(gtype, pusher, getter)		\
  else if (type == G_TYPE_ ## gtype)			\
    {							\
      pusher (L, g_value_ ## getter (val));		\
      return;						\
    }

  HANDLE_GTYPE(BOOLEAN, lua_pushboolean, get_boolean)
  HANDLE_GTYPE(CHAR, lua_pushinteger, get_char)
  HANDLE_GTYPE(UCHAR, lua_pushinteger, get_uchar)
  HANDLE_GTYPE(INT, lua_pushinteger, get_int)
  HANDLE_GTYPE(UINT, lua_pushnumber, get_uint)
  HANDLE_GTYPE(LONG, lua_pushnumber, get_long)
  HANDLE_GTYPE(ULONG, lua_pushnumber, get_ulong)
  HANDLE_GTYPE(INT64, lua_pushnumber, get_int64)
  HANDLE_GTYPE(UINT64, lua_pushnumber, get_uint64)
  HANDLE_GTYPE(FLOAT, lua_pushnumber, get_float)
  HANDLE_GTYPE(DOUBLE, lua_pushnumber, get_double)
  HANDLE_GTYPE(GTYPE, lua_pushnumber, get_gtype)
  HANDLE_GTYPE(STRING, lua_pushstring, get_string)
  HANDLE_GTYPE(ENUM, lua_pushinteger, get_enum)
  HANDLE_GTYPE(FLAGS, lua_pushinteger, get_flags)
#undef HANDLE_GTYPE

  /* If we have typeinfo, try to use it for some specific cases. */
  if (ti != NULL)
    {
      GITypeTag tag = g_type_info_get_tag (ti);
      switch (tag)
	{
	case GI_TYPE_TAG_GHASH:
	  marshal_2lua_hash (L, ti, GI_TRANSFER_NOTHING,
			     g_value_get_boxed (val));
	  return;

	case GI_TYPE_TAG_GLIST:
	case GI_TYPE_TAG_GSLIST:
	  {
	    marshal_2lua_list (L, ti, tag, GI_TRANSFER_NOTHING,
			       g_value_get_pointer (val));
	    return;
	  }

	default:
	  break;
	}
    }

  /* Handle other cases. */
  switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_ENUM:
      lua_pushinteger (L, g_value_get_enum (val));
      return;

    case G_TYPE_FLAGS:
      lua_pushinteger (L, g_value_get_flags (val));
      return;

    case G_TYPE_OBJECT:
    case G_TYPE_BOXED:
      {
	GIBaseInfo *bi = g_irepository_find_by_gtype (NULL, type);
	if (bi != NULL)
	  {
	    gpointer obj = GI_IS_OBJECT_INFO (bi) ?
	      g_value_dup_object (val) : g_value_dup_boxed (val);
	    lgi_compound_create (L, bi, obj, TRUE, 0);
	    g_base_info_unref (bi);
	    return;
	  }
	break;
      }

    default:
      {
	/* Fundamentals handling. */
	GIBaseInfo *info = g_irepository_find_by_gtype (NULL, type);
	if (info != NULL)
	  {
	    if (g_base_info_get_type (info) == GI_INFO_TYPE_OBJECT
		&& g_object_info_get_fundamental (info))
	      {
		GIObjectInfoGetValueFunction get_value;
		GIObjectInfoRefFunction ref;
		get_value =
		  g_object_info_get_get_value_function_pointer (info);
		ref = g_object_info_get_ref_function_pointer (info);
		if (get_value != NULL && ref != NULL)
		  {
		    gpointer obj = get_value (val);
		    if (obj != NULL)
		      ref (obj);
		    lgi_compound_create (L, info, obj, TRUE, 0);
		    g_base_info_unref (info);
		    return;
		  }
	      }
	    g_base_info_unref (info);
	  }
      }
    }

  luaL_error (L, "g_value_get: no handling of %s(%s)",
	      g_type_name (type), g_type_name (G_TYPE_FUNDAMENTAL (type)));
}
