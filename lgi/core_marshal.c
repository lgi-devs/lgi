/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2011 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implements marshalling as virtual machine running marshalling 'scripts'
 */

#include <string.h>
#include "lgi.h"

typedef enum _Marshal
  {
    MARSHAL_TYPE_MASK       = 0x0000000f,
    MARSHAL_TYPE_INT        = 0,
    MARSHAL_TYPE_FLOAT      = 1,
    MARSHAL_TYPE_BOOLEAN    = 2,
    MARSHAL_TYPE_STRING     = 3,

    MARSHAL_TYPE_RECORD     = 4,
    MARSHAL_TYPE_OBJECT     = 5,

    MARSHAL_TYPE_CARRAY     = 6,
    MARSHAL_TYPE_GARRAY     = 7,
    MARSHAL_TYPE_GBYTEARRAY = 8,
    MARSHAL_TYPE_GLIST      = 9,
    MARSHAL_TYPE_GHASHTABLE = 10,

    MARSHAL_TYPE_CALLABLE   = 11,

    MARSHAL_TYPE_PTR        = 12,

    MARSHAL_TYPE_DIRECT     = 13,

    MARSHAL_SUBTYPE_MASK    = 0x00000070,

    MARSHAL_SUBTYPE_NUMBER_SIZE       = 0x00000030,
    MARSHAL_SUBTYPE_NUMBER_SIZE_SHIFT = 4,

    MARSHAL_SUBTYPE_INT_SIGNED = 0x00000040,

    MARSHAL_SUBTYPE_STRING_FILENAME = 0x00000010,

    MARSHAL_SUBTYPE_VALUE   = 0x00000000,
    MARSHAL_SUBTYPE_REF     = 0x00000010,

    MARSHAL_SUBTYPE_CALLABLE_MANUAL   = 0x00000000,
    MARSHAL_SUBTYPE_CALLABLE_CALL     = 0x00000010,
    MARSHAL_SUBTYPE_CALLABLE_ASYNC    = 0x00000020,
    MARSHAL_SUBTYPE_CALLABLE_NOTIFIED = 0x00000030,

    MARSHAL_TRANSFER_OWNERSHIP = 0x00000040,
    MARSHAL_ALLOW_NIL          = 0x00000080,

    MARSHAL_CODE_MASK      = 0x00000300,
    MARSHAL_CODE_SHIFT     = 8,
    MARSHAL_CODE_END       = 0x00000000,
    MARSHAL_CODE_CREATE    = 0x00000100,
    MARSHAL_CODE_TO_LUA    = 0x00000200,
    MARSHAL_CODE_TO_C      = 0x00000300,

    MARSHAL_CODE_INPUT_MASK  = 0x0000f000,
    MARSHAL_CODE_INPUT_SHIFT = 12,

    MARSHAL_CODE_NATIVE_MASK  = 0xffff0000,
    MARSHAL_CODE_NATIVE_SHIFT = 16,
  } Marhal;

static void
marshal_2lua_int (lua_State *L, int *temps, guint32 type, gpointer native)
{
  GIArgument *arg = native;
  switch (type & MARSHAL_SUBTYPE_MASK)
    {
#define HANDLE_INT(sign, size, name)			\
      case (sign ? MARSHAL_SUBTYPE_INT_SIGNED : 0)	\
	| (size << MARSHAL_SUBTYPE_NUMBER_SIZE_SHIFT):	\
	lua_pushnumber (L, arg->v_ ## name);		\
	break

      HANDLE_INT(1, 0, int8);
      HANDLE_INT(1, 1, int16);
      HANDLE_INT(1, 2, int32);
      HANDLE_INT(1, 3, int64);
      HANDLE_INT(0, 0, uint8);
      HANDLE_INT(0, 1, uint16);
      HANDLE_INT(0, 2, uint32);
      HANDLE_INT(0, 3, uint64);

#undef HANDLE_INT

    default:
      g_assert_not_reached ();
    }

  if (*temps > 0)
    lua_insert (L, - *temps - 1);
}

static void
marshal_2c_int (lua_State *L, guint32 type, int input, gpointer native)
{
  /* Get number from the Lua side inputs. */
  GIArgument *arg = native;
  lua_Number number = luaL_checknumber (L, input);
  lua_Number low_limit, high_limit;
  switch (type & MARSHAL_SUBTYPE_MASK)
    {
#define HANDLE_INT(sign, size, name, low, high)		\
      case (sign ? MARSHAL_SUBTYPE_INT_SIGNED : 0)	\
	| (size << MARSHAL_SUBTYPE_NUMBER_SIZE_SHIFT):	\
	arg->v_ ## name = number;			\
	low_limit = low;				\
	high_limit = high;				\
	break

      HANDLE_INT(1, 0, int8, -0x80, 0x7f);
      HANDLE_INT(1, 1, int16, -0x8000, 0x7fff);
      HANDLE_INT(1, 2, int32, -0x80000000LL, 0x7fffffffLL);
      HANDLE_INT(1, 3, int64,
		 ((lua_Number) -0x7f00000000000000LL) - 1,
		 0x7fffffffffffffffLL);
      HANDLE_INT(0, 0, uint8, 0, 0xff);
      HANDLE_INT(0, 1, uint16, 0, 0xffff);
      HANDLE_INT(0, 2, uint32, 0, 0xffffffffUL);
      HANDLE_INT(0, 3, uint64, 0, 0xffffffffffffffffULL);

#undef HANDLE_INT

    default:
      g_assert_not_reached ();
    }

  /* Check that the number falls into the limits. */
  if (number < low_limit || number > high_limit)
    {
      lua_pushfstring (L, "%f is out of <%f, %f>",
		       number, low_limit, high_limit);
      luaL_argerror (L, input, lua_tostring (L, -1));
    }
}

static void
marshal_2lua_float (lua_State *L, int *temps, guint32 type, gpointer native)
{
  GIArgument *arg = native;
  switch (type & MARSHAL_SUBTYPE_MASK)
    {
#define HANDLE_FLOAT(size, name)			\
      case size << MARSHAL_SUBTYPE_NUMBER_SIZE_SHIFT:	\
	lua_pushnumber (L, arg->v_ ## name);		\
	break

      HANDLE_FLOAT(2, float);
      HANDLE_FLOAT(3, double);

#undef HANDLE_FLOAT

    default:
      g_assert_not_reached ();
    }

  if (*temps > 0)
    lua_insert (L, - *temps - 1);
}

static void
marshal_2c_float (lua_State *L, guint32 type, int input, gpointer native)
{
  /* Get number from the Lua side inputs. */
  GIArgument *arg = native;
  switch (type & MARSHAL_SUBTYPE_MASK)
    {
#define HANDLE_FLOAT(size, name)			\
      case size << MARSHAL_SUBTYPE_NUMBER_SIZE_SHIFT:	\
	arg->v_ ## name = luaL_checknumber (L, input);	\
	break

      HANDLE_FLOAT(2, float);
      HANDLE_FLOAT(3, double);

#undef HANDLE_FLOAT

    default:
      g_assert_not_reached ();
    }
}

static void
marshal_2lua_boolean (lua_State *L, int *temps, guint32 type, gpointer native)
{
  GIArgument *arg = native;
  lua_pushboolean (L, arg->v_boolean);
  if (*temps > 0)
    lua_insert (L, - *temps - 1);
}

static void
marshal_2c_boolean (lua_State *L, guint32 type, int input, gpointer native)
{
  GIArgument *arg = native;
  arg->v_boolean = lua_toboolean (L, input);
}

static void
marshal_2lua_string (lua_State *L, int *temps, guint32 type, gpointer native)
{
  GIArgument *arg = native;
  gchar *str = arg->v_string;
  if (type & MARSHAL_SUBTYPE_STRING_FILENAME)
    {
      gchar *filename = g_filename_to_utf8 (str, -1, NULL, NULL, NULL);
      lua_pushstring (L, filename);
      g_free (filename);
    }
  else
    lua_pushstring (L, str);

  if (type & MARSHAL_TRANSFER_OWNERSHIP)
    g_free (str);

  if (*temps > 0)
    lua_insert (L, - *temps - 1);
}

static void
marshal_2c_string (lua_State *L, int *temps, guint32 type, int input,
		   gpointer native)
{
  const gchar *str;
  GIArgument *arg = native;
  if (lua_isnoneornil (L, input) && (type & MARSHAL_ALLOW_NIL) != 0)
    {
      arg->v_string = NULL;
      return;
    }

  str = luaL_checkstring (L, input);
  if (type & MARSHAL_SUBTYPE_STRING_FILENAME)
    {
      /* Convert from filename encoding and create temporary guard for
	 newly created filename string. */
      str = g_filename_from_utf8 (str, -1, NULL, NULL, NULL);
      if ((type & MARSHAL_TRANSFER_OWNERSHIP) == 0)
	{
	  *lgi_guard_create (L, g_free) = (gpointer) str;
	  (*temps)++;
	}
    }
  else if (type & MARSHAL_TRANSFER_OWNERSHIP)
    str = g_strdup (str);
  arg->v_string = (gchar *) str;
}

static void
marshal_2lua_record (lua_State *L, int code_index, int *code_pos, int *temps,
		     guint32 type, gpointer native, int parent)
{
  /* Handle ref/value difference. */
  if ((type & MARSHAL_SUBTYPE_MASK) == MARSHAL_SUBTYPE_REF)
    native = ((GIArgument *) native)->v_pointer;

  /* Get record type and marshal record instance. */
  lua_rawgeti (L, code_index, (*code_pos)++);
  lgi_record_2lua (L, native, type & MARSHAL_TRANSFER_OWNERSHIP, parent);
  if (*temps > 0)
    lua_insert (L, - *temps - 1);
}

static void
marshal_2c_record (lua_State *L, int code_index, int *code_pos, guint32 type,
		   int input, gpointer native)
{
  gsize size = 0;
  gpointer record;

  /* Get record type. */
  lua_rawgeti (L, code_index, (*code_pos)++);
  if ((type & MARSHAL_SUBTYPE_MASK) == MARSHAL_SUBTYPE_VALUE)
    {
      lua_getfield (L, -1, "_size");
      size = lua_tointeger (L, -1);
      g_assert (size > 0);
      lua_pop (L, 1);
    }

  /* Get record type and marshal record instance. */
  record = lgi_record_2c (L, input, type & MARSHAL_ALLOW_NIL, FALSE);
  if (size == 0)
    /* Assign pointer to return address. */
    ((GIArgument *) native)->v_pointer = record;
  else
    /* Copy contents of the record into the target. */
    memcpy (native, record, size);
}

static void
marshal_2lua_object (lua_State *L, int code_index, int *code_pos, int *temps,
		     guint32 type, gpointer native)
{
  /* Just skip type record, it is unused in current implementation. */
  (*code_pos)++;

  /* Marshal object to lua. */
  lgi_object_2lua (L, ((GIArgument *) native)->v_pointer,
		   type & MARSHAL_TRANSFER_OWNERSHIP);
  if (*temps > 0)
    lua_insert (L, - *temps - 1);
}

static void
marshal_2c_object (lua_State *L, int code_index, int *code_pos, guint32 type,
		   int input, gpointer native)
{
  GIArgument *arg = native;
  GType gtype;

  /* Get object type. */
  lua_rawgeti (L, code_index, (*code_pos)++);
  lua_getfield (L, -1, "_gtype");
  gtype = lua_tonumber (L, -1);
  lua_pop (L, 1);

  /* Get record type and marshal record instance. */
  arg->v_pointer = lgi_object_2c (L, input, gtype,
				  type & MARSHAL_ALLOW_NIL, FALSE);
}

typedef void
(*marshal_code_fun)(lua_State *L, int code_index, int *code_pos, int *temps,
		    guint32 type, int input, gpointer native);

static void
marshal_2lua (lua_State *L, int code_index, int *code_pos, int *temps,
	      guint32 type, int input, gpointer native)
{
  switch (type & MARSHAL_TYPE_MASK)
    {
    case MARSHAL_TYPE_INT:
      marshal_2lua_int (L, temps, type, native);
      break;
    case MARSHAL_TYPE_FLOAT:
      marshal_2lua_float (L, temps, type, native);
      break;
    case MARSHAL_TYPE_BOOLEAN:
      marshal_2lua_boolean (L, temps, type, native);
      break;
    case MARSHAL_TYPE_STRING:
      marshal_2lua_string (L, temps, type, native);
      break;
    case MARSHAL_TYPE_RECORD:
      marshal_2lua_record (L, code_index, code_pos, temps,
			   type, native, 0);
      break;
    case MARSHAL_TYPE_OBJECT:
      marshal_2lua_object (L, code_index, code_pos, temps, type, native);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
marshal_2c (lua_State *L, int code_index, int *code_pos, int *temps,
	    guint32 type, int input, gpointer native)
{
  switch (type & MARSHAL_TYPE_MASK)
    {
    case MARSHAL_TYPE_INT:
      marshal_2c_int (L, type, input, native);
      break;
    case MARSHAL_TYPE_FLOAT:
      marshal_2c_float (L, type, input, native);
      break;
    case MARSHAL_TYPE_BOOLEAN:
      marshal_2c_boolean (L, type, input, native);
      break;
    case MARSHAL_TYPE_STRING:
      marshal_2c_string (L, temps, type, input, native);
      break;
    case MARSHAL_TYPE_RECORD:
      marshal_2c_record (L, code_index, code_pos, type, input, native);
      break;
    case MARSHAL_TYPE_OBJECT:
      marshal_2c_object (L, code_index, code_pos, type, input, native);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
marshal_create (lua_State *L, int code_index, int *code_pos, int *temps,
		guint32 type, int input, gpointer native)
{
  switch (type & MARSHAL_TYPE_MASK)
    {
    case MARSHAL_TYPE_DIRECT:
      /* Get direct value from codetype and leave it on the stack. */
      lua_rawgeti (L, code_index, (*code_pos)++);
      break;

    case MARSHAL_TYPE_RECORD:
      /* Create new record instance from the type in the codetable. */
      lua_rawgeti (L, code_index, (*code_pos)++);
      lgi_record_new (L);
      break;

    case MARSHAL_TYPE_CARRAY:
      break;

    default:
      g_assert_not_reached ();
    }
}

static const marshal_code_fun marshal_code[] = {
  /* MARSHAL_CODE_END */    NULL,
  /* MARSHAL_CODE_CREATE */ marshal_create,
  /* MARSHAL_CODE_TO_LUA */ marshal_2lua,
  /* MARSHAL_CODE_TO_C */   marshal_2c
};

int
lgi_marshal (lua_State *L, int code_index, int *code_pos,
	     int inputs_base, gpointer native_base)
{
  int temps = 0;
  guint32 type;
  marshal_code_fun handler;
  gpointer native;
  gsize offset;
  int input;

  /* Iterate through the type stream. */
  luaL_checkstack (L, 1, NULL);
  for (;;)
    {
      /* Retrieve the instruction from the stream. */
      lua_rawgeti (L, code_index, (*code_pos)++);
      type = (guint32) lua_tointeger (L, -1);
      lua_pop (L, 1);

      /* Prepare native address with displacement. */
      offset = type >> MARSHAL_CODE_NATIVE_SHIFT;
      native = (offset == 0xffff) ? NULL : (guint8 *) native_base + offset;

      /* Prepare input argument offset. */
      offset = (type & MARSHAL_CODE_INPUT_MASK) >> MARSHAL_CODE_INPUT_SHIFT;
      input = inputs_base + offset;
      if (native == NULL)
	/* Get address from the input. */
	native = *(gpointer *) lua_touserdata (L, input);

      /* Invoke proper code handler. */
      luaL_checkstack (L, 4, NULL);
      handler = marshal_code[(type & MARSHAL_CODE_MASK) >> MARSHAL_CODE_SHIFT];
      if (!handler)
	return temps;
      handler(L, code_index, code_pos, &temps, type, input, native);
    }
}

static const struct luaL_Reg marshal_api_reg[] = {
  { NULL, NULL }
};

void
lgi_core_marshal_init (lua_State *L)
{
  /* Create 'marshal' API table in main core API table. */
  lua_newtable (L);
  luaL_register (L, NULL, marshal_api_reg);
  lua_setfield (L, -2, "core_marshal");
}
