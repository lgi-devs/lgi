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
    MARSHAL_TYPE_BASE_MASK       = 0x0000000f,
    MARSHAL_TYPE_BASE_INT        = 0,
    MARSHAL_TYPE_BASE_FLOAT      = 1,
    MARSHAL_TYPE_BASE_BOOLEAN    = 2,
    MARSHAL_TYPE_BASE_STRING     = 3,
    MARSHAL_TYPE_BASE_RECORD     = 4,
    MARSHAL_TYPE_BASE_OBJECT     = 5,
    MARSHAL_TYPE_BASE_ARRAY      = 6,
    MARSHAL_TYPE_BASE_LIST       = 7,
    MARSHAL_TYPE_BASE_HASHTABLE  = 8,
    MARSHAL_TYPE_BASE_CALLABLE   = 9,
    MARSHAL_TYPE_BASE_PTR        = 10,
    MARSHAL_TYPE_BASE_DIRECT     = 11,

    MARSHAL_TYPE_IS_POINTER         = 0x00000010,
    MARSHAL_TYPE_TRANSFER_OWNERSHIP = 0x00000020,
    MARSHAL_TYPE_ALLOW_NIL          = 0x00000040,

    MARSHAL_TYPE_NUMBER_SIZE_MASK   = 0x00000060,
    MARSHAL_TYPE_NUMBER_SIZE_SHIFT  = 5,
    MARSHAL_TYPE_NUMBER_UNSIGNED    = 0x00000080,

    MARSHAL_TYPE_STRING_FILENAME    = 0x00000080,

    MARSHAL_TYPE_ARRAY_MASK         = 0x00000180,
    MARSHAL_TYPE_ARRAY_C            = 0x00000000,
    MARSHAL_TYPE_ARRAY_GARRAY       = 0x00000080,
    MARSHAL_TYPE_ARRAY_GPTRARRAY    = 0x00000100,
    MARSHAL_TYPE_ARRAY_GBYTEARRAY   = 0x00000180,

    MARSHAL_TYPE_LIST_MASK         = 0x00000080,
    MARSHAL_TYPE_LIST_GSLIST       = 0x00000000,
    MARSHAL_TYPE_LIST_GLIST        = 0x00000080,

    MARSHAL_TYPE_CALLABLE_MASK     = 0x00000180,
    MARSHAL_TYPE_CALLABLE_BOUND    = 0x00000000,
    MARSHAL_TYPE_CALLABLE_CALL     = 0x00000080,
    MARSHAL_TYPE_CALLABLE_ASYNC    = 0x00000100,
    MARSHAL_TYPE_CALLABLE_NOTIFIED = 0x00000180,

    MARSHAL_CODE_MASK      = 0x00000600,
    MARSHAL_CODE_SHIFT     = 9,
    MARSHAL_CODE_END       = 0x00000000,
    MARSHAL_CODE_CREATE    = 0x00000200,
    MARSHAL_CODE_TO_LUA    = 0x00000400,
    MARSHAL_CODE_TO_C      = 0x00000600,

    MARSHAL_CODE_INPUT_POP   = 0x00000800,
    MARSHAL_CODE_INPUT_MASK  = 0x0000f000,
    MARSHAL_CODE_INPUT_SHIFT = 12,

    MARSHAL_CODE_NATIVE_MASK  = 0xffff0000,
    MARSHAL_CODE_NATIVE_SHIFT = 16,
  } Marhal;

static void
marshal_2lua (lua_State *L, int code_index, int *code_pos, int *temps,
	      guint32 type, int input, gpointer native);


static void
marshal_2lua_int (lua_State *L, int *temps, guint32 type, gpointer native)
{
  GIArgument *arg = native;
  switch (type & (MARSHAL_TYPE_NUMBER_SIZE_MASK | MARSHAL_TYPE_NUMBER_UNSIGNED))
    {
#define HANDLE_INT(sign, size, name)			\
      case (sign ? 0 : MARSHAL_TYPE_NUMBER_UNSIGNED)    \
	| (size << MARSHAL_TYPE_NUMBER_SIZE_SHIFT):	\
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

  lua_insert (L, -(*temps + 1));
}

static void
marshal_2c_int (lua_State *L, guint32 type, int input, gpointer native)
{
  /* Get number from the Lua side inputs. */
  GIArgument *arg = native;
  lua_Number number = luaL_checknumber (L, input);
  lua_Number low_limit, high_limit;
  switch (type & (MARSHAL_TYPE_NUMBER_SIZE_MASK | MARSHAL_TYPE_NUMBER_UNSIGNED))
    {
#define HANDLE_INT(sign, size, name, low, high)		\
      case (sign ? 0 : MARSHAL_TYPE_NUMBER_UNSIGNED)	\
	| (size << MARSHAL_TYPE_NUMBER_SIZE_SHIFT):	\
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
  switch (type & MARSHAL_TYPE_NUMBER_SIZE_MASK)
    {
#define HANDLE_FLOAT(size, name)			\
      case size << MARSHAL_TYPE_NUMBER_SIZE_SHIFT:	\
	lua_pushnumber (L, arg->v_ ## name);		\
	break

      HANDLE_FLOAT(2, float);
      HANDLE_FLOAT(3, double);

#undef HANDLE_FLOAT

    default:
      g_assert_not_reached ();
    }

  lua_insert (L, -(*temps + 1));
}

static void
marshal_2c_float (lua_State *L, guint32 type, int input, gpointer native)
{
  /* Get number from the Lua side inputs. */
  GIArgument *arg = native;
  switch (type & MARSHAL_TYPE_NUMBER_SIZE_MASK)
    {
#define HANDLE_FLOAT(size, name)			\
      case size << MARSHAL_TYPE_NUMBER_SIZE_SHIFT:	\
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
  lua_insert (L, -(*temps + 1));
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
  if (type & MARSHAL_TYPE_STRING_FILENAME)
    {
      gchar *filename = g_filename_to_utf8 (str, -1, NULL, NULL, NULL);
      lua_pushstring (L, filename);
      g_free (filename);
    }
  else
    lua_pushstring (L, str);

  if (type & MARSHAL_TYPE_TRANSFER_OWNERSHIP)
    g_free (str);

  lua_insert (L, -(*temps + 1));
}

static void
marshal_2c_string (lua_State *L, int *temps, guint32 type, int input,
		   gpointer native)
{
  const gchar *str;
  GIArgument *arg = native;
  if (lua_isnoneornil (L, input) && (type & MARSHAL_TYPE_ALLOW_NIL) != 0)
    {
      arg->v_string = NULL;
      return;
    }

  str = luaL_checkstring (L, input);
  if (type & MARSHAL_TYPE_STRING_FILENAME)
    {
      /* Convert from filename encoding and create temporary guard for
	 newly created filename string. */
      str = g_filename_from_utf8 (str, -1, NULL, NULL, NULL);
      if ((type & MARSHAL_TYPE_TRANSFER_OWNERSHIP) == 0)
	{
	  *lgi_guard_create (L, g_free) = (gpointer) str;
	  (*temps)++;
	}
    }
  else if (type & MARSHAL_TYPE_TRANSFER_OWNERSHIP)
    str = g_strdup (str);
  arg->v_string = (gchar *) str;
}

static void
marshal_2lua_record (lua_State *L, int code_index, int *code_pos, int *temps,
		     guint32 type, gpointer native, int parent)
{
  /* Handle ref/value difference. */
  if (type & MARSHAL_TYPE_IS_POINTER)
    native = ((GIArgument *) native)->v_pointer;

  /* Get record type and marshal record instance. */
  lua_rawgeti (L, code_index, (*code_pos)++);
  lgi_record_2lua (L, native, type & MARSHAL_TYPE_TRANSFER_OWNERSHIP, parent);
  lua_insert (L, -(*temps + 1));
}

static void
marshal_2c_record (lua_State *L, int code_index, int *code_pos, guint32 type,
		   int input, gpointer native)
{
  gsize size = 0;
  gpointer record;

  /* Get record type. */
  lua_rawgeti (L, code_index, (*code_pos)++);
  if ((type & MARSHAL_TYPE_IS_POINTER) == 0)
    {
      lua_getfield (L, -1, "_size");
      size = lua_tointeger (L, -1);
      g_assert (size > 0);
      lua_pop (L, 1);
    }

  /* Get record type and marshal record instance. */
  record = lgi_record_2c (L, input, type & MARSHAL_TYPE_ALLOW_NIL, FALSE);
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
		   type & MARSHAL_TYPE_TRANSFER_OWNERSHIP);
  lua_insert (L, -(*temps + 1));
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
				  type & MARSHAL_TYPE_ALLOW_NIL, FALSE);
}

static gssize
marshal_get_size (lua_State *L, guint32 *type, int code_index, int code_pos,
                  int param_index)
{
  gssize size = sizeof (gpointer);

  /* Get the type opcode. */
  lua_rawgeti (L, code_index, code_pos);
  *type = lua_tonumber (L, -1);
  lua_pop (L, 1);

  /* Decide according the real type. */
  switch (*type & MARSHAL_TYPE_BASE_MASK)
    {
    case MARSHAL_TYPE_BASE_INT:
    case MARSHAL_TYPE_BASE_FLOAT:
      /* Size is encoded in the subtype. */
      size = 1 << ((*type & MARSHAL_TYPE_NUMBER_SIZE_MASK)
                   >> MARSHAL_TYPE_NUMBER_SIZE_SHIFT);
      break;

    case MARSHAL_TYPE_BASE_BOOLEAN:
      size = sizeof (gboolean);
      break;

    case MARSHAL_TYPE_BASE_RECORD:
      if ((*type & MARSHAL_TYPE_IS_POINTER) == 0)
        {
          /* Get real record size. */
          lua_rawgeti (L, code_index, ++code_pos);
          lua_getfield (L, -1, "_size");
          size = lua_tonumber (L, -1);
          lua_pop (L, 2);
        }
      break;

    case MARSHAL_TYPE_BASE_ARRAY:
      /* Special handling for fixed-size C arrays stored by-value. */
      if ((*type & MARSHAL_TYPE_ARRAY_MASK) == MARSHAL_TYPE_ARRAY_C
          && (*type & MARSHAL_TYPE_IS_POINTER) == 0)
        {
          /* Try to get real array size. */
          gssize element;
          guint32 element_type;
          luaL_checkstack (L, 2, NULL);
          element = marshal_get_size (L, &element_type, code_index,
                                      code_pos + 1, param_index - 1);
          size = element * lua_tointeger (L, param_index);
        }
      break;
    }

  return size;
}

static void
marshal_2lua_array (lua_State *L, int code_index, int *code_pos, int *temps,
		    guint32 type, gpointer native)
{
  int pos, index;
  gssize length, element_size;
  const guint8* data;
  guint32 element_type;

  /* Remember code_pos, because we will iterate through it while
     marshalling elements. */
  pos = *code_pos;

  /* Get element size of the array. */
  element_size = marshal_get_size (L, &element_type, code_index, pos,
                                   -(*temps + 1));

  /* Get length (in elts) and base array pointer. */
  if ((type & MARSHAL_TYPE_ARRAY_MASK) == MARSHAL_TYPE_ARRAY_C)
    {
      /* Get length from last marshalled item, and remove that item. */
      length = lua_tointeger (L, -(*temps + 1));
      lua_remove (L, -(*temps + 1));
      data = native;
    }
  else
    {
      /* Get length from native array. */
      length = ((GArray *) native)->len;
      data = (const guint8 *) ((GArray *) native)->data;
    }

  if (element_size == 1
      && (element_type & MARSHAL_TYPE_BASE_MASK) == MARSHAL_TYPE_BASE_INT)
    {
      /* Arrays of 8bit integers are translated into simple strings. */
      lua_pushlstring (L, (const char *) data,
                       length >= 0 ? length : strlen ((const char *) data));
      lua_insert (L, -(*temps + 1));
    }
  else
    {
      /* Create the target table, marshal elements inside one by one. */
      lua_createtable (L, length >= 0 ? length : 0, 0);
      lua_insert (L, -(*temps + 1));
      for (index = 0; length >=0 && index < length; ++index)
        {
          /* Reset subtype code position for each iteration. */
          *code_pos = pos;

          /* Marshal single array element into Lua. */
          marshal_2lua (L, code_index, code_pos, temps, element_type, 0,
                        (gpointer) data + index * element_size);

          /* Store marshalled element into the results table. */
          lua_pushvalue (L, -(*temps + 1));
          lua_rawseti (L, -(*temps + 3), index + 1);
          lua_remove (L, -(*temps + 1));
        }
    }

  /* If the ownership was transferred, destroy the old array. */
  if (type & MARSHAL_TYPE_TRANSFER_OWNERSHIP)
    {
      switch (type & MARSHAL_TYPE_ARRAY_MASK)
        {
        case MARSHAL_TYPE_ARRAY_C:
          g_free (native);
          break;
        case MARSHAL_TYPE_ARRAY_GARRAY:
          g_array_free (native, TRUE);
          break;
        case MARSHAL_TYPE_ARRAY_GPTRARRAY:
          g_ptr_array_free (native, TRUE);
          break;
        case MARSHAL_TYPE_ARRAY_GBYTEARRAY:
          g_byte_array_free (native, TRUE);
          break;
        }
    }
}

typedef void
(*marshal_code_fun)(lua_State *L, int code_index, int *code_pos, int *temps,
		    guint32 type, int input, gpointer native);

static void
marshal_2lua (lua_State *L, int code_index, int *code_pos, int *temps,
	      guint32 type, int input, gpointer native)
{
  luaL_checkstack (L, 4, NULL);
  switch (type & MARSHAL_TYPE_BASE_MASK)
    {
    case MARSHAL_TYPE_BASE_INT:
      marshal_2lua_int (L, temps, type, native);
      break;
    case MARSHAL_TYPE_BASE_FLOAT:
      marshal_2lua_float (L, temps, type, native);
      break;
    case MARSHAL_TYPE_BASE_BOOLEAN:
      marshal_2lua_boolean (L, temps, type, native);
      break;
    case MARSHAL_TYPE_BASE_STRING:
      marshal_2lua_string (L, temps, type, native);
      break;
    case MARSHAL_TYPE_BASE_RECORD:
      marshal_2lua_record (L, code_index, code_pos, temps,
			   type, native, 0);
      break;
    case MARSHAL_TYPE_BASE_OBJECT:
      marshal_2lua_object (L, code_index, code_pos, temps, type, native);
      break;
    case MARSHAL_TYPE_BASE_ARRAY:
      marshal_2lua_array (L, code_index, code_pos, temps, type, native);
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
marshal_2c (lua_State *L, int code_index, int *code_pos, int *temps,
	    guint32 type, int input, gpointer native)
{
  luaL_checkstack (L, 4, NULL);
  switch (type & MARSHAL_TYPE_BASE_MASK)
    {
    case MARSHAL_TYPE_BASE_INT:
      marshal_2c_int (L, type, input, native);
      break;
    case MARSHAL_TYPE_BASE_FLOAT:
      marshal_2c_float (L, type, input, native);
      break;
    case MARSHAL_TYPE_BASE_BOOLEAN:
      marshal_2c_boolean (L, type, input, native);
      break;
    case MARSHAL_TYPE_BASE_STRING:
      marshal_2c_string (L, temps, type, input, native);
      break;
    case MARSHAL_TYPE_BASE_RECORD:
      marshal_2c_record (L, code_index, code_pos, type, input, native);
      break;
    case MARSHAL_TYPE_BASE_OBJECT:
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
  luaL_checkstack (L, 2, NULL);
  switch (type & MARSHAL_TYPE_BASE_MASK)
    {
    case MARSHAL_TYPE_BASE_DIRECT:
      /* Get direct value from codetype and leave it on the stack. */
      lua_rawgeti (L, code_index, (*code_pos)++);
      break;

    case MARSHAL_TYPE_BASE_RECORD:
      /* Create new record instance from the type in the codetable. */
      lua_rawgeti (L, code_index, (*code_pos)++);
      lgi_record_new (L);
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

  luaL_checkstack (L, 1, NULL);
  lgi_makeabs (L, inputs_base);
  lgi_makeabs (L, code_index);

  /* Iterate through the type stream. */
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
      if (offset != MARSHAL_CODE_INPUT_SHIFT)
        {
          input = inputs_base + offset;
          if (native == NULL)
            /* Get address from the input. */
            native = *(gpointer *) lua_touserdata (L, input);
        }
      else
        /* Input for this operand is the last output to be popped. */
        input = lua_gettop (L) - temps;

      /* Invoke proper code handler. */
      handler = marshal_code[(type & MARSHAL_CODE_MASK) >> MARSHAL_CODE_SHIFT];
      if (!handler)
	return temps;
      handler(L, code_index, code_pos, &temps, type, input, native);

      /* If the input should be removed after processing, do it now. */
      if (type & MARSHAL_CODE_INPUT_POP)
        lua_remove (L, input);
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
