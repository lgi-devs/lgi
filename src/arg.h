/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Author: Pavel Holejsovsky (pavel.holejsovsky@gmail.com)
 *
 * License: MIT.
 */

/* File is meant to be included multiple times, include guard is missing. */

/* Define describes property of given type.

#define TYPE_SIMPLE(tag, ctype, argf, dtor, push, check)

tag      GI_TYPE_TAG_ ## tag
ctype    Name of C-style typedef
argf     Appropriate field in GArgument union
dtor     Function call which destroys the type
push     lua push_xxx method
check    lua check_xxx method
*/

#ifndef TYPE_NOP
#define TYPE_NOP(x) (void)0
#endif

TYPE_SIMPLE(BOOLEAN, 
	    gboolean, 
	    v_boolean, 
	    TYPE_NOP,
	    lua_pushboolean, 
	    lua_toboolean)

TYPE_SIMPLE(INT8,    
	    gint8,    
	    v_int8,    
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(UINT8,   
	    guint8,   
	    v_uint8,   
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(INT16,   
	    gint16,   
	    v_int16,   
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(UINT16,  
	    guint16,  
	    v_uint16,  
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(INT32,   
	    gint32,   
	    v_int32,   
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(UINT32,  
	    guint32,  
	    v_uint32,  
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(INT64,   
	    gint64,   
	    v_int64,   
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(UINT64,  
	    guint64,  
	    v_uint64,  
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(FLOAT,   
	    gfloat,   
	    v_float,   
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(DOUBLE,  
	    gdouble,  
	    v_double,  
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(SHORT,   
	    gshort,   
	    v_short,   
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(USHORT,  
	    gushort,  
	    v_ushort,  
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(INT,     
	    gint,     
	    v_int,     
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(UINT,    
	    guint,    
	    v_uint,    
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(LONG,    
	    glong,    
	    v_long,    
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(ULONG,   
	    gulong,   
	    v_ulong,   
	    TYPE_NOP,
	    lua_pushnumber,  
	    luaL_checknumber)

TYPE_SIMPLE(SSIZE,   
	    gssize,   
	    v_ssize,   
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(SIZE,    
	    gsize,    
	    v_size,    
	    TYPE_NOP,
	    lua_pushnumber, 
	    luaL_checknumber)

TYPE_SIMPLE(GTYPE,   
	    GType,    
	    v_long,    
	    TYPE_NOP,
	    lua_pushinteger, 
	    luaL_checkinteger)

TYPE_SIMPLE(UTF8,    
	    gpointer, 
	    v_pointer, 
	    g_free, 
	    lua_pushstring,  
	    luaL_checkstring)

#undef TYPE_SIMPLE
