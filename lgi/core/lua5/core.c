/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010, 2011, 2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Core C utility API.
 */

#include <string.h>
#include "lgi.h"

#ifndef NDEBUG
const char *lgi_sd (lua_State *L)
{
  int i;
  static gchar *msg = 0;
  g_free (msg);
  msg = g_strdup ("");
  int top = lua_gettop (L);
  for (i = 1; i <= top; i++)
    {
      int t = lua_type (L, i);
      gchar *item, *nmsg;
      switch (t)
	{
	case LUA_TSTRING:
	  item = g_strdup_printf ("`%s'", lua_tostring (L, i));
	  break;

	case LUA_TBOOLEAN:
	  item = g_strdup_printf (lua_toboolean (L, i) ? "true" : "false");
	  break;

	case LUA_TNUMBER:
	  item = g_strdup_printf ("%g", lua_tonumber (L, i));
	  break;

	default:
	  item = g_strdup_printf ("%s(%p)", lua_typename (L, t),
				  lua_topointer (L, i));
	  break;
	}
      nmsg = g_strconcat (msg, " ", item, NULL);
      g_free (msg);
      g_free (item);
      msg = nmsg;
    }
  return msg;
}
#endif

#if LUA_VERSION_NUM < 502
void lua_rawsetp (lua_State *L, int index, void *p)
{
  index = lua_absindex (L, index);
  lua_pushlightuserdata (L, p);
  lua_insert (L, -2);
  lua_rawset (L, index);
}

void lua_rawgetp (lua_State *L, int index, void *p)
{
  index = lua_absindex (L, index);
  lua_pushlightuserdata (L, p);
  lua_rawget (L, index);
}

void *
luaL_testudata (lua_State *L, int narg, const char *name)
{
  void *udata = NULL;
  luaL_checkstack (L, 2, NULL);
  narg = lua_absindex (L, narg);
  if (lua_getmetatable (L, narg))
    {
      luaL_getmetatable (L, name);
      if (lua_equal (L, -1, -2))
	udata = lua_touserdata (L, narg);
      lua_pop (L, 2);
    }
  return udata;
}
#endif

void *luaL_testudatap (lua_State *L, int arg, void *p)
{
  void *ptr = lua_touserdata (L, arg);
  if (ptr != NULL)
    {
      if (lua_getmetatable (L, arg))
	{
	  lua_rawgetp (L, LUA_REGISTRYINDEX, p);
	  if (!lua_rawequal (L, -1, -2))
	    ptr = NULL;
	  lua_pop (L, 2);
	  return ptr;
	}
    }
  return NULL;
}

void *luaL_checkudatap (lua_State *L, int arg, void *p)
{
  void *ptr = luaL_testudatap (L, arg, p);
  if (ptr == NULL)
    luaL_argerror (L, arg, "expected specific userdata");
  return ptr;
}

void
lgi_cache_create (lua_State *L, gpointer key, const char *mode)
{
  lua_pushlightuserdata (L, key);
  lua_newtable (L);
  if (mode)
    {
      lua_newtable (L);
      lua_pushstring (L, mode);
      lua_setfield (L, -2, "__mode");
      lua_setmetatable (L, -2);
    }
  lua_rawset (L, LUA_REGISTRYINDEX);
}

typedef struct _LgiStateMutex
{
  /* Pointer to either local state lock (next member of this
     structure) or to global package lock. */
  GStaticRecMutex *mutex;
  GStaticRecMutex state_mutex;
} LgiStateMutex;

/* Global package lock (the one used for
   gdk_threads_enter/clutter_threads_enter) */
static GStaticRecMutex package_mutex = G_STATIC_REC_MUTEX_INIT;

/* GC method for GStaticRecMutex structure, which lives inside lua_State. */
static int
call_mutex_gc (lua_State* L)
{
  LgiStateMutex *mutex = lua_touserdata (L, 1);
  g_static_rec_mutex_unlock (mutex->mutex);
  g_static_rec_mutex_free (&mutex->state_mutex);
  return 0;
}

/* lightuserdata of address of this member is key to LUA_REGISTRYINDEX
   where CallMutex instance for this state resides. */
static int call_mutex;

gpointer
lgi_state_get_lock (lua_State *L)
{
  gpointer state_lock;
  lua_pushlightuserdata (L, &call_mutex);
  lua_gettable (L, LUA_REGISTRYINDEX);
  state_lock = lua_touserdata (L, -1);
  lua_pop (L, 1);
  return state_lock;
}

void
lgi_state_enter (gpointer state_lock)
{
  LgiStateMutex *mutex = state_lock;
  GStaticRecMutex *wait_on;

  /* There is a complication with lock switching.  During the wait for
     the lock, someone could call core.registerlock() and thus change
     the lock protecting the state.  Accomodate for this situation. */
  for (;;)
    {
      wait_on = g_atomic_pointer_get (&mutex->mutex);
      g_static_rec_mutex_lock (wait_on);
      if (wait_on == mutex->mutex)
	break;

      /* The lock is changed, unlock this one and wait again. */
      g_static_rec_mutex_unlock (wait_on);
    }
}

void
lgi_state_leave (gpointer state_lock)
{
  /* Get pointer to the call mutex belonging to this state. */
  LgiStateMutex *mutex = state_lock;
  g_static_rec_mutex_unlock (mutex->mutex);
}

static const char* log_levels[] = {
  "ERROR", "CRITICAL", "WARNING", "MESSAGE", "INFO", "DEBUG", "???", NULL
};

static int
core_log (lua_State *L)
{
  const char *domain = luaL_checkstring (L, 1);
  int level = 1 << (luaL_checkoption (L, 2, log_levels[5], log_levels) + 2);
  const char *message = luaL_checkstring (L, 3);
  g_log (domain, level, "%s", message);
  return 0;
}

static int
core_yield (lua_State *L)
{
  /* Perform yield with unlocked mutex; this might force another
     threads waiting on the mutex to perform what they need to do
     (i.e. enter Lua with callbacks). */
  gpointer state_lock = lgi_state_get_lock (L);
  lgi_state_leave (state_lock);
  g_thread_yield ();
  lgi_state_enter (state_lock);
  return 0;
}

static void
package_lock_enter (void)
{
  g_static_rec_mutex_lock (&package_mutex);
}

static void
package_lock_leave (void)
{
  g_static_rec_mutex_unlock (&package_mutex);
}

static gpointer package_lock_register[8] = { NULL };

static int
core_registerlock (lua_State *L)
{
  void (*set_lock_functions)(GCallback, GCallback);
  LgiStateMutex *mutex;
  GStaticRecMutex *wait_on;
  unsigned i;

  /* Get registration function. */
  luaL_checktype (L, 1, LUA_TLIGHTUSERDATA);
  set_lock_functions = lua_touserdata (L, 1);
  luaL_argcheck (L, set_lock_functions != NULL, 1, "NULL function");

  /* Check, whether this package was already registered. */
  for (i = 0; i < G_N_ELEMENTS (package_lock_register) &&
	 package_lock_register[i] != set_lock_functions; i++)
    {
      if (package_lock_register[i] == NULL)
	{
	  /* Register our package lock functions. */
	  package_lock_register[i] = set_lock_functions;
	  set_lock_functions (package_lock_enter, package_lock_leave);
	  break;
	}
    }

  /* Switch our statelock to actually use packagelock. */
  lua_pushlightuserdata (L, &call_mutex);
  lua_rawget (L, LUA_REGISTRYINDEX);
  mutex = lua_touserdata (L, -1);
  wait_on = g_atomic_pointer_get (&mutex->mutex);
  if (wait_on != &package_mutex)
    {
      g_static_rec_mutex_lock (&package_mutex);
      g_atomic_pointer_set (&mutex->mutex, &package_mutex);
      g_static_rec_mutex_unlock (wait_on);
    }
  return 0;
}

static int
core_band (lua_State *L)
{
  lua_pushnumber (L, (unsigned)luaL_checknumber (L, 1)
		  & (unsigned)luaL_checknumber (L, 2));
  return 1;
}

static int
core_bor (lua_State *L)
{
  lua_pushnumber (L, (unsigned)luaL_checknumber (L, 1)
		  | (unsigned)luaL_checknumber (L, 2));
  return 1;
}

#define UD_MODULE "lgi.core.module"

static int
module_gc (lua_State *L)
{
  GModule **module = luaL_checkudata (L, 1, UD_MODULE);
  g_module_close (*module);
  return 0;
}

static int
module_index (lua_State *L)
{
  GModule **module = luaL_checkudata (L, 1, UD_MODULE);
  gpointer address;
  if (g_module_symbol (*module, luaL_checkstring (L, 2), &address))
    {
      lua_pushlightuserdata (L, address);
      return 1;
    }

  lua_pushnil (L);
  lua_pushstring (L, g_module_error ());
  return 2;
}

static const struct luaL_Reg module_reg[] = {
  { "__gc", module_gc },
  { "__index", module_index },
  { NULL, NULL }
};

#ifdef G_WITH_CYGWIN
#define MODULE_NAME_FORMAT_VERSION "cyg%s-%d.dll"
#define MODULE_NAME_FORMAT_PLAIN "cyg%s.dll"
#elif G_OS_WIN32
#define MODULE_NAME_FORMAT_VERSION "lib%s-%d.dll"
#define MODULE_NAME_FORMAT_PLAIN "lib%s.dll"
#else
#define MODULE_NAME_FORMAT_VERSION "lib%s.so.%d"
#define MODULE_NAME_FORMAT_PLAIN "lib%s.so"
#endif

/* Creates 'module' object which resolves symbol names to
   lightuserdata addresses.
   module, path = core.module(basename[, version]) */
static int
core_module (lua_State *L)
{
  char *name;

  /* If the version is present, combine it with basename. */
  if (!lua_isnoneornil (L, 2))
    name = g_strdup_printf (MODULE_NAME_FORMAT_VERSION,
			    luaL_checkstring (L, 1),
			    (int) luaL_checkinteger (L, 2));
  else
    name = g_strdup_printf (MODULE_NAME_FORMAT_PLAIN,
			    luaL_checkstring (L, 1));

  /* Try to load the module. */
  GModule *module = g_module_open (name, 0);
  if (module == NULL)
    {
      lua_pushnil (L);
      goto end;
    }

  /* Embed the module in the userdata for the module. */
  *(GModule **) lua_newuserdata (L, sizeof (module)) = module;
  luaL_getmetatable (L, UD_MODULE);
  lua_setmetatable (L, -2);

 end:
  lua_pushstring (L, name);
  g_free (name);
  return 2;
}

static const struct luaL_Reg lgi_reg[] = {
  { "log",  core_log },
  { "yield", core_yield },
  { "registerlock", core_registerlock },
  { "band", core_band },
  { "bor", core_bor },
  { "module", core_module },
  { NULL, NULL }
};

static void
set_resident (lua_State *L)
{
  /* Get '_CLIBS' table from the registry (Lua5.2). */
  lua_getfield (L, LUA_REGISTRYINDEX, "_CLIBS");
  if (!lua_isnil (L, -1))
    {
      /* Remove the very last item in they array part, which is handle
	 to our loaded module used by _CLIBS.gctm to clean modules
	 upon state cleanup. But before removing it, check, that it is
	 really the handle of our module.  Our module filename is
	 passed as arg 2. */
      lua_pushvalue (L, 2);
      lua_gettable (L, -2);
      lua_rawgeti (L, -2, lua_objlen (L, -2));
      if (lua_equal (L, -1, -2))
	{
	  lua_pushnil (L);
	  lua_rawseti (L, -4, lua_objlen (L, -4));
	}
      lua_pop (L, 3);
      return;
    }
  else
    {
      /* This hack tries to enumerate the whole registry table and
	 find 'LOADLIB: path' library.  When it detects itself, it
	 just removes pointer to the loaded library, disallowing Lua
	 to close it, thus leaving it resident even when the state is
	 closed. */

      /* Note: 'nil' is on the stack from lua_getfield() call above. */
      while (lua_next (L, LUA_REGISTRYINDEX))
	{
	  if (lua_type (L, -2) == LUA_TSTRING)
	    {
	      const char *str = lua_tostring (L, -2);
	      if (g_str_has_prefix (str, "LOADLIB: ") &&
		  strstr (str, "lgilua5"))
		{
		  /* NULL the pointer to the loaded library. */
		  if (lua_type (L, -1) == LUA_TUSERDATA)
		    {
		      gpointer *lib = lua_touserdata (L, -1);
		      *lib = NULL;
		    }

		  /* Clean the stack and return. */
		  lua_pop (L, 2);
		  return;
		}
	    }

	  lua_pop (L, 1);
	}
    }
}

int
luaopen_lgi_corelgilua51 (lua_State* L)
{
  LgiStateMutex *mutex;

  /* Try to make itself resident.  This is needed because this dynamic
     module is 'statically' linked with glib/gobject, and these
     libraries are not designed to be unloaded.  Once they are
     unloaded, they cannot be safely loaded again into the same
     process.  To avoid problems when repeately opening and closing
     lua_States and loading lgi into them, we try to make the whole
     'core' module resident. */
  set_resident (L);

  /* Early GLib initializations. Make sure that following fundamental
     G_TYPEs are already initialized. */
  g_type_init ();
  volatile GType unused;
  unused = G_TYPE_DATE;
  unused = G_TYPE_REGEX;
  unused = G_TYPE_DATE_TIME;
  unused = G_TYPE_VARIANT_TYPE;
  unused = G_TYPE_STRV;
  unused = unused;

  /* Register 'module' metatable. */
  luaL_newmetatable (L, UD_MODULE);
  luaL_setfuncs (L, module_reg);
  lua_pop (L, 1);

  /* Create call mutex guard, keep it locked initially (it is unlocked
     only when we are calling out to GObject-C code) and store it into
     the registry. */
  mutex = lua_newuserdata (L, sizeof (*mutex));
  mutex->mutex = &mutex->state_mutex;
  g_static_rec_mutex_init (&mutex->state_mutex);
  g_static_rec_mutex_lock (&mutex->state_mutex);
  lua_newtable (L);
  lua_pushcfunction (L, call_mutex_gc);
  lua_setfield (L, -2, "__gc");
  lua_setmetatable (L, -2);
  lua_rawsetp (L, LUA_REGISTRYINDEX, &call_mutex);

  /* Register 'lgi.core' interface. */
  lua_newtable (L);
  luaL_setfuncs (L, lgi_reg);

  /* Initialize modules. */
  lgi_buffer_init (L);
  lgi_aggr_init (L);
  lgi_ctype_init (L);
  lgi_compound_init (L);
  lgi_call_init (L);

  /* Return registration table. */
  return 1;
}
