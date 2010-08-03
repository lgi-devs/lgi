/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

/* File is meant to be included multiple times, include guard is missing. */

/* Define describes property of given type.

#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt,
                    val_type, val_get, val_set)

tag      GI_TYPE_TAG_ ## tag
ctype    Name of C-style typedef
argf     Appropriate field in GArgument union
dtor     Function call which destroys the type
push     lua push_xxx method
check    lua check_xxx method
opt      lua opt_xxx method
val_type g_type for storing in GValue
val_get  g_value getter of this type
val_set  g_value setter of this type
*/

#define DECLTYPE_NOP(x) (void)0
#define DECLTYPE_OPTBOOLEAN(L, narg, def) lua_toboolean(L, narg)

DECLTYPE(GI_TYPE_TAG_BOOLEAN,
         gboolean,
         v_boolean,
         DECLTYPE_NOP,
         lua_pushboolean,
         lua_toboolean,
         DECLTYPE_OPTBOOLEAN,
         G_TYPE_BOOLEAN,
         g_value_get_boolean,
         g_value_set_boolean)

DECLTYPE(GI_TYPE_TAG_INT8,
         gint8,
         v_int8,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_CHAR,
         g_value_get_char,
         g_value_set_char)

DECLTYPE(GI_TYPE_TAG_UINT8,
         guint8,
         v_uint8,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_UCHAR,
         g_value_get_uchar,
         g_value_set_uchar)

DECLTYPE(GI_TYPE_TAG_INT16,
         gint16,
         v_int16,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int)

DECLTYPE(GI_TYPE_TAG_UINT16,
         guint16,
         v_uint16,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint)

DECLTYPE(GI_TYPE_TAG_INT32,
         gint32,
         v_int32,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int)

DECLTYPE(GI_TYPE_TAG_UINT32,
         guint32,
         v_uint32,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint)

DECLTYPE(GI_TYPE_TAG_INT64,
         gint64,
         v_int64,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_INT64,
         g_value_get_int64,
         g_value_set_int64)

DECLTYPE(GI_TYPE_TAG_UINT64,
         guint64,
         v_uint64,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_UINT64,
         g_value_get_uint64,
         g_value_set_uint64)

DECLTYPE(GI_TYPE_TAG_FLOAT,
         gfloat,
         v_float,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_FLOAT,
         g_value_get_float,
         g_value_set_float)

DECLTYPE(GI_TYPE_TAG_DOUBLE,
         gdouble,
         v_double,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_DOUBLE,
         g_value_get_double,
         g_value_set_double)

DECLTYPE(GI_TYPE_TAG_SHORT,
         gshort,
         v_short,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int)

DECLTYPE(GI_TYPE_TAG_USHORT,
         gushort,
         v_ushort,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint)

DECLTYPE(GI_TYPE_TAG_INT,
         gint,
         v_int,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int)

DECLTYPE(GI_TYPE_TAG_UINT,
         guint,
         v_uint,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint)

DECLTYPE(GI_TYPE_TAG_LONG,
         glong,
         v_long,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_LONG,
         g_value_get_long,
         g_value_set_long)

DECLTYPE(GI_TYPE_TAG_ULONG,
         gulong,
         v_ulong,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_ULONG,
         g_value_get_ulong,
         g_value_set_ulong)

DECLTYPE(GI_TYPE_TAG_SSIZE,
         gssize,
         v_ssize,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int)

DECLTYPE(GI_TYPE_TAG_SIZE,
         gsize,
         v_size,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint)

DECLTYPE(GI_TYPE_TAG_GTYPE,
         GType,
         v_long,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         G_TYPE_GTYPE,
         g_value_get_gtype,
         g_value_set_gtype)

DECLTYPE(GI_TYPE_TAG_UTF8,
         gchar*,
         v_string,
         g_free,
         lua_pushstring,
         luaL_checkstring,
         luaL_optstring,
         G_TYPE_STRING,
         (gchar*)g_value_get_string,
         g_value_set_string)

#undef DECLTYPE
#undef DECLTYPE_NOP
