/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

/* File is meant to be included multiple times, include guard is missing. */

/* Define describes property of given type.

#define TYPE_SIMPLE(tag, ctype, argf, dtor, push, check, 
                    val_type, val_get, val_set)

tag      GI_TYPE_TAG_ ## tag
ctype    Name of C-style typedef
argf     Appropriate field in GArgument union
dtor     Function call which destroys the type
push     lua push_xxx method
check    lua check_xxx method
val_type g_type for storing in GValue
val_get  g_value getter of this type
val_set  g_value setter of this type
*/

#ifndef TYPE_NOP
#define TYPE_NOP(x) (void)0
#endif

TYPE_SIMPLE(GI_TYPE_TAG_BOOLEAN,
	    gboolean,
	    v_boolean,
	    TYPE_NOP,
	    lua_pushboolean,
	    lua_toboolean,
	    G_TYPE_BOOLEAN,
	    g_value_get_boolean,
	    g_value_set_boolean)

TYPE_SIMPLE(GI_TYPE_TAG_INT8,
	    gint8,
	    v_int8,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_CHAR,
	    g_value_get_char,
	    g_value_set_char)

TYPE_SIMPLE(GI_TYPE_TAG_UINT8,
	    guint8,
	    v_uint8,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_UCHAR,
	    g_value_get_uchar,
	    g_value_set_uchar)

TYPE_SIMPLE(GI_TYPE_TAG_INT16,
	    gint16,
	    v_int16,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_INT,
	    g_value_get_int,
	    g_value_set_int)

TYPE_SIMPLE(GI_TYPE_TAG_UINT16,
	    guint16,
	    v_uint16,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_UINT,
	    g_value_get_uint,
	    g_value_set_uint)

TYPE_SIMPLE(GI_TYPE_TAG_INT32,
	    gint32,
	    v_int32,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_INT,
	    g_value_get_int,
	    g_value_set_int)

TYPE_SIMPLE(GI_TYPE_TAG_UINT32,
	    guint32,
	    v_uint32,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_UINT,
	    g_value_get_uint,
	    g_value_set_uint)

TYPE_SIMPLE(GI_TYPE_TAG_INT64,
	    gint64,
	    v_int64,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_INT64,
	    g_value_get_int64,
	    g_value_set_int64)

TYPE_SIMPLE(GI_TYPE_TAG_UINT64,
	    guint64,
	    v_uint64,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_UINT64,
	    g_value_get_uint64,
	    g_value_set_uint64)

TYPE_SIMPLE(GI_TYPE_TAG_FLOAT,
	    gfloat,
	    v_float,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_FLOAT,
	    g_value_get_float,
	    g_value_set_float)

TYPE_SIMPLE(GI_TYPE_TAG_DOUBLE,
	    gdouble,
	    v_double,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_DOUBLE,
	    g_value_get_double,
	    g_value_set_double)

TYPE_SIMPLE(GI_TYPE_TAG_SHORT,
	    gshort,
	    v_short,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_INT,
	    g_value_get_int,
	    g_value_set_int)

TYPE_SIMPLE(GI_TYPE_TAG_USHORT,
	    gushort,
	    v_ushort,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_UINT,
	    g_value_get_uint,
	    g_value_set_uint)

TYPE_SIMPLE(GI_TYPE_TAG_INT,
	    gint,
	    v_int,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_INT,
	    g_value_get_int,
	    g_value_set_int)

TYPE_SIMPLE(GI_TYPE_TAG_UINT,
	    guint,
	    v_uint,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_UINT,
	    g_value_get_uint,
	    g_value_set_uint)

TYPE_SIMPLE(GI_TYPE_TAG_LONG,
	    glong,
	    v_long,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_LONG,
	    g_value_get_long,
	    g_value_set_long)

TYPE_SIMPLE(GI_TYPE_TAG_ULONG,
	    gulong,
	    v_ulong,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_ULONG,
	    g_value_get_ulong,
	    g_value_set_ulong)

TYPE_SIMPLE(GI_TYPE_TAG_SSIZE,
	    gssize,
	    v_ssize,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_INT,
	    g_value_get_int,
	    g_value_set_int)

TYPE_SIMPLE(GI_TYPE_TAG_SIZE,
	    gsize,
	    v_size,
	    TYPE_NOP,
	    lua_pushnumber,
	    luaL_checknumber,
	    G_TYPE_UINT,
	    g_value_get_uint,
	    g_value_set_uint)

TYPE_SIMPLE(GI_TYPE_TAG_GTYPE,
	    GType,
	    v_long,
	    TYPE_NOP,
	    lua_pushinteger,
	    luaL_checkinteger,
	    G_TYPE_GTYPE,
	    g_value_get_gtype,
	    g_value_set_gtype)

TYPE_SIMPLE(GI_TYPE_TAG_UTF8,
	    gpointer,
	    v_pointer,
	    g_free,
	    lua_pushstring,
	    luaL_checkstring,
	    G_TYPE_STRING,
	    (gchar*)g_value_get_string,
	    g_value_set_string)

#undef TYPE_SIMPLE
#undef TYPE_NOP
