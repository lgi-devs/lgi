/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

/* File is meant to be included multiple times, include guard is missing. */

/* Define describes property of given type.

#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,
                 val_type, val_get, val_set, ffi_type)

tag      GI_TYPE_TAG_ ## tag
ctype    Name of C-style typedef
argf     Appropriate field in GArgument union
dtor     Function call which destroys the type
push     lua push_xxx method
check    lua check_xxx method
opt      lua opt_xxx method
dup      value duplication method, e.g. g_strdup
val_type g_type for storing in GValue
val_get  g_value getter of this type
val_set  g_value setter of this type

*/

#define DECLTYPE_NOP(x) (void)0
#define DECLTYPE_IDENTITY(x) x
#define DECLTYPE_OPTBOOLEAN(L, narg, def) lua_toboolean(L, narg)

DECLTYPE(GI_TYPE_TAG_BOOLEAN,
         gboolean,
         v_boolean,
         DECLTYPE_NOP,
         lua_pushboolean,
         lua_toboolean,
         DECLTYPE_OPTBOOLEAN,
         DECLTYPE_IDENTITY,
         G_TYPE_BOOLEAN,
         g_value_get_boolean,
         g_value_set_boolean,
         ffi_type_uint)

DECLTYPE(GI_TYPE_TAG_INT8,
         gint8,
         v_int8,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         DECLTYPE_IDENTITY,
         G_TYPE_CHAR,
         g_value_get_char,
         g_value_set_char,
         ffi_type_sint8)

DECLTYPE(GI_TYPE_TAG_UINT8,
         guint8,
         v_uint8,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         DECLTYPE_IDENTITY,
         G_TYPE_UCHAR,
         g_value_get_uchar,
         g_value_set_uchar,
         ffi_type_uint8)

DECLTYPE(GI_TYPE_TAG_INT16,
         gint16,
         v_int16,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         DECLTYPE_IDENTITY,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int,
         ffi_type_sint16)

DECLTYPE(GI_TYPE_TAG_UINT16,
         guint16,
         v_uint16,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         DECLTYPE_IDENTITY,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint,
         ffi_type_uint16)

DECLTYPE(GI_TYPE_TAG_INT32,
         gint32,
         v_int32,
         DECLTYPE_NOP,
         lua_pushinteger,
         luaL_checkinteger,
         luaL_optinteger,
         DECLTYPE_IDENTITY,
         G_TYPE_INT,
         g_value_get_int,
         g_value_set_int,
         ffi_type_sint32)

DECLTYPE(GI_TYPE_TAG_UINT32,
         guint32,
         v_uint32,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         DECLTYPE_IDENTITY,
         G_TYPE_UINT,
         g_value_get_uint,
         g_value_set_uint,
         ffi_type_uint32)

DECLTYPE(GI_TYPE_TAG_INT64,
         gint64,
         v_int64,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         DECLTYPE_IDENTITY,
         G_TYPE_INT64,
         g_value_get_int64,
         g_value_set_int64,
         ffi_type_sint64)

DECLTYPE(GI_TYPE_TAG_UINT64,
         guint64,
         v_uint64,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         DECLTYPE_IDENTITY,
         G_TYPE_UINT64,
         g_value_get_uint64,
         g_value_set_uint64,
         ffi_type_uint64)

DECLTYPE(GI_TYPE_TAG_FLOAT,
         gfloat,
         v_float,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         DECLTYPE_IDENTITY,
         G_TYPE_FLOAT,
         g_value_get_float,
         g_value_set_float,
         ffi_type_float)

DECLTYPE(GI_TYPE_TAG_DOUBLE,
         gdouble,
         v_double,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checknumber,
         luaL_optnumber,
         DECLTYPE_IDENTITY,
         G_TYPE_DOUBLE,
         g_value_get_double,
         g_value_set_double,
         ffi_type_double)

#if GLIB_SIZEOF_SIZE_T == 4
DECLTYPE(GI_TYPE_TAG_GTYPE,
         GType,
         v_size,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checklong,
         luaL_optlong,
         DECLTYPE_IDENTITY,
         G_TYPE_GTYPE,
         g_value_get_gtype,
         g_value_set_gtype,
         ffi_type_uint32)
#else
DECLTYPE(GI_TYPE_TAG_GTYPE,
         GType,
         v_size,
         DECLTYPE_NOP,
         lua_pushnumber,
         luaL_checklong,
         luaL_optlong,
         DECLTYPE_IDENTITY,
         G_TYPE_GTYPE,
         g_value_get_gtype,
         g_value_set_gtype,
         ffi_type_uint64)
#endif

#ifndef DECLTYPE_NUMERIC_ONLY
DECLTYPE(GI_TYPE_TAG_UTF8,
         gchar*,
         v_string,
         g_free,
         lua_pushstring,
         luaL_checkstring,
         luaL_optstring,
         g_strdup,
         G_TYPE_STRING,
         (gchar*)g_value_get_string,
         g_value_set_string,
         ffi_type_pointer)

DECLTYPE(GI_TYPE_TAG_FILENAME,
         gchar*,
         v_string,
         g_free,
         lua_pushstring,
         luaL_checkstring,
         luaL_optstring,
         g_strdup,
         G_TYPE_STRING,
         (gchar*)g_value_get_string,
         g_value_set_string,
         ffi_type_pointer)
#endif

#undef DECLTYPE
#undef DECLTYPE_NOP
#undef DECLTYPE_IDENTITY
#undef DECLTYPE_NUMERIC_ONLY
