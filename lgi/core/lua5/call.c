/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010-2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Implementation of call gates between Lua and GLib/C.
 */

#include "lgi.h"
#include <string.h>
#include <ffi.h>

typedef struct _Dir
{
  /* Number of Lua input for this argument, 0 if this is not input. */
  guint16 in : 8;

  /* Number of Lua output for this argument, 0 if this is not output. */
  guint16 out : 8;
} Dir;

typedef struct _CallInfo
{
  /* libffi compiled info for function. */
  ffi_cif cif;

  /* Total number of arguments, incl. return value. */
  guint32 n_args : 8;

  /* Number of redirection slots needed to allocate. */
  guint32 n_redirs : 8;

  /* Default guard size for closures. */
  guint32 guard_size : 8;

  /* Array of Dir instances for the arguments. */
  Dir dirs[1];
} CallInfo;

/* Closure as allocated by ffi_closure_allocate() */
typedef struct _Closure
{
  /* libffi closure object. */
  ffi_closure closure;

  /* Lua context to be used. */
  lua_State *L;

  /* State lock. */
  gpointer state_lock;

  /* Pointer to associated call_info. */
  CallInfo *call_info;
} Closure;

/* metatable of boxed pointer to Closure.  This userdata also has
   assigned uservalue table, containing: { [1] = thread, [2] =
   targetfunc, [3] = callinfo }. It is basically glue holding all
   parts of closure together. */
static int closure_mt;

enum {
  CLOSURE_ENV_THREAD = 1,
  CLOSURE_ENV_TARGET = 2,
  CLOSURE_ENV_CALLINFO = 3
};

/* Registry index of [addr(Closure) -> weak(ClosureBox)] table. */
static int closure_index;

static const char *ffi_names[] = {
  "sint8", "uint8", "sint16", "uint16",
  "sint32", "uint32", "sint64", "uint64",
  "float", "double", "pointer", "void",
  NULL
};

/* Must correspond to ffi_names array. */
static ffi_type *ffi_types[] = {
  &ffi_type_sint8, &ffi_type_uint8, &ffi_type_sint16, &ffi_type_uint16,
  &ffi_type_sint32, &ffi_type_uint32, &ffi_type_sint64, &ffi_type_uint64,
  &ffi_type_float, &ffi_type_double, &ffi_type_pointer, &ffi_type_void
};

/* Creates libffi definition block useful for calling functions and
   creating trampolines.
   call_info = call.new(defs, ti[, guard_size])
   - defs: an array of string representing libffi types.
   - ti: typeinfo definition table, to be assigned to created data.
   - guard_size: size of the guard creation.
   - target: target function for closure. */
static int
call_new (lua_State *L)
{
  int i, n_args;
  CallInfo *call_info;
  Dir *dir;
  ffi_type **type;

  luaL_checktype (L, 1, LUA_TTABLE);
  luaL_checktype (L, 2, LUA_TTABLE);

  /* Allocate CallInfo userdata structure. */
  n_args = luaL_len (L, 1) / 3;

  call_info = lua_newuserdata (L, G_STRUCT_OFFSET (CallInfo, dirs)
			       + n_args * (sizeof (ffi_type *)
					   + sizeof (Dir)));
  call_info->n_args = n_args;
  call_info->n_redirs = 0;
  call_info->guard_size = luaL_optnumber (L, 3, 0);
  lua_pushvalue (L, 2);
  lua_setuservalue (L, -2);
  dir = call_info->dirs;
  type = (ffi_type **)(dir + n_args);

  /* Fill ffi_type and dir array from defs table. */
  for (i = 0; i < n_args; i++)
    {
      lua_rawgeti (L, 1, (i * 3) + 1);
      type[i] = ffi_types[luaL_checkoption (L, -1, NULL, ffi_names)];
      lua_rawgeti (L, 1, (i * 3) + 2);
      dir[i].in = luaL_checknumber (L, -1);
      lua_rawgeti (L, 1, (i * 3) + 3);
      dir[i].out = luaL_checknumber (L, -1);
      if (i != 0 && dir[i].out != 0)
	call_info->n_redirs++;
      lua_pop (L, 3);
    }

  /* Initialize libffi cif. */
  if (ffi_prep_cif (&call_info->cif, FFI_DEFAULT_ABI, n_args - 1,
		    type[0], &type[1])
      != FFI_OK)
    return luaL_error (L, "Failed to ffi_prep_cif()");

  return 1;
}

/* Calls from Lua to C.
   ret1[, ret2, ...] = call.toc(call_info, addr, guard, ...)
   - call_info: function definition created by call.prepare()
   - addr: lightuserdata with address to call
   - guard: guard to be used during marshalling
   - ...: input arguments */
static int
call_toc (lua_State *L)
{
  int i, arg, ntipos, nti;
  CallInfo *call_info;
  LgiCTypeGuard *guard;
  gpointer addr;
  GIArgument *args;
  gpointer *ffi_args;
  gpointer *redirs = NULL;
  gsize size, align;
  gpointer state_lock = lgi_state_get_lock (L);

  /* Pick up arguments. */
  call_info = lua_touserdata (L, 1);
  addr = lua_touserdata (L, 2);
  luaL_argcheck (L, addr != NULL, 2, "NULL target");
  guard = lua_touserdata (L, 3);
  lua_getuservalue (L, 2);
  nti = lua_absindex (L, -1);

  /* Allocate marshalling areas. */
  args = g_newa (GIArgument, call_info->n_args);
  ffi_args = g_newa (gpointer, call_info->n_args - 1);
  redirs = g_newa (gpointer, call_info->n_redirs);

  /* Go through arguments and marshal inputs. */
  arg = ntipos = 1;
  for (i = 1; i < call_info->n_args; i++)
    {
      /* Prepare ffi slot pointer, either direct pointer to args or
	 through redirect if output variant is used. */
      if (call_info->dirs[i].out == 0)
	/* ffi slot points directly to the arg. */
	ffi_args[i - 1] = &args[i];
      else
	{
	  /* ffi slot goes through redirect. */
	  args[i].v_pointer = NULL;
	  *redirs = &args[i];
	  ffi_args[i - 1] = *redirs++;
	}

      if (call_info->dirs[i].in != 0)
	/* Marshal input argument. */
	lgi_ctype_2c (L, guard, nti, &ntipos,
		      call_info->dirs[i].in, arg++, &args[i]);
      else
	/* Skip the typeinfo. */
	lgi_ctype_query (L, nti, &ntipos, &size, &align);
    }

  /* All marshalled, commit the guard. */
  lgi_ctype_guard_commit (L, guard);

  /* Call through libffi. */
  lgi_state_leave (state_lock);
  ffi_call (&call_info->cif, addr, &args[0], ffi_args);
  lgi_state_enter (state_lock);

  /* Unmarshal return value and output arguments. */
  arg = 0;
  ntipos = 1;
  for (i = 0; i < call_info->n_args; i++)
    {
      if (call_info->dirs[i].out != 0)
	{
	  /* Marshal output argument. */
	  lgi_ctype_2lua (L, guard, nti, &ntipos,
			  -call_info->dirs[i].out, 0, &args[i]);
	  arg++;
	}
      else
	/* Skip the typeinfo. */
	lgi_ctype_query (L, nti, &ntipos, &size, &align);
    }

  /* All marshalled, commit the guard. */
  lgi_ctype_guard_commit (L, guard);

  /* Return number of output arguments created. */
  return arg;
}

static int
closure_gc (lua_State *L)
{
  Closure **closure = luaL_checkudatap (L, 1, &closure_mt);
  if (*closure != NULL)
    ffi_closure_free (&(*closure)->closure);
  return 0;
}

/* Callable module public API table. */
static const luaL_Reg closure_mt_reg[] = {
  { "__gc", closure_gc },
  { NULL, NULL }
};

static void
closure_callback (ffi_cif *cif, void *ret, void **args, void *closure_arg)
{
  (void) cif;
  int stacktop, i, nti, ntipos, n_items, item_pos;
  Closure *closure = closure_arg;
  CallInfo *call_info = closure->call_info;
  lua_State *L;
  gsize size, align;
  LgiCTypeGuard *guard;

  /* Get access to proper Lua context to be used for the call/resume. */
  lgi_state_enter (closure->state_lock);
  L = closure->L;
  lua_rawgetp (L, LUA_REGISTRYINDEX, &closure_index);
  lua_rawgetp (L, -1, closure);
  lua_getuservalue (L, -1);
  if (G_LIKELY (lua_status (L) == 0))
    {
      /* Target thread is fine, suffle stack so that there is only ti
	 and target function entries on it. */
      lua_replace (L, -3);
      lua_pop (L, 1);
      lua_rawgeti (L, -1, CLOSURE_ENV_TARGET);
    }
  else
    {
      /* Target thread is suspended, it is not allowed to lua_call()
	 into it.  Create new thread and store it into CallInfo
	 instance. */
      L = lua_newthread (L);
      lua_rawseti (closure->L, -2, CLOSURE_ENV_THREAD);
      lua_rawgeti (closure->L, -1, CLOSURE_ENV_TARGET);
      lua_xmove (closure->L, L, 2);
      lua_pop (closure->L, 2);
      closure->L = L;
    }

  /* Create guard, shove it beneath target function on the stack. */
  guard = lgi_ctype_guard_create (L, call_info->guard_size);
  lua_insert (L, -2);

  /* Remember stacktop, this is the position to which we should rewind
     the stack to when leaving.  We already have ti table, guard and
     target function to call on the stack, so count with it. */
  stacktop = lua_gettop (L) - 3;

  /* Marshal input arguments to Lua. */
  nti = stacktop;
  ntipos = 1;
  n_items = 0;
  for (i = 1; i < call_info->n_args; i++)
    {
      if (call_info->dirs[i].in != 0)
	{
	  /* Marshal input value. */
	  lgi_ctype_2lua (L, guard, nti, &ntipos,
			  call_info->dirs[i].in, 0, args[i - 1]);
	  n_items++;
	}
      else
	/* Skip typeinfo. */
	lgi_ctype_query (L, nti, &ntipos, &size, &align);
    }

  /* All marshalled, commit the guard. */
  lgi_ctype_guard_commit (L, guard);

  /* Call into Lua. */
  lua_call (L, n_items, LUA_MULTRET);
  ntipos = 1;
  item_pos = stacktop + 2;

  /* Marshal return value from Lua. */
  if (call_info->dirs[0].out != 0)
    lgi_ctype_2c (L, guard, nti, &ntipos, -call_info->dirs[0].out,
		  item_pos++, ret);
  else
    lgi_ctype_query (L, nti, &ntipos, &size, &align);

  /* Marshal output arguments from Lua. */
  for (i = 1; i < call_info->n_args; i++)
    {
      if (call_info->dirs[i].out != 0)
	lgi_ctype_2c (L, guard, nti, &ntipos, -call_info->dirs[i].out,
		      item_pos++, args[i - 1]);
      else
	lgi_ctype_query (L, nti, &ntipos, &size, &align);
    }

  /* All marshalled, commit the guard. */
  lgi_ctype_guard_commit (L, guard);

  /* Before returning, restore the stack. */
  lua_settop (L, stacktop);
  lgi_state_leave (closure->state_lock);
}

/* Creates closure block from given call definition and target.
   closure, caddr = call.tolua(call_info, target)
   - call_info: prepared by call.new()
   - target: target function to be called */
static int
call_tolua (lua_State *L)
{
  Closure **closure;
  gpointer call_addr;

  /* Allocate boxed wrapper for the closure. */
  closure = lua_newuserdata (L, sizeof (Closure *));
  *closure = NULL;
  lua_newtable (L);
  lua_pushthread (L);
  lua_rawseti (L, -2, CLOSURE_ENV_THREAD);
  lua_pushvalue (L, 2);
  lua_rawseti (L, -2, CLOSURE_ENV_TARGET);
  lua_pushvalue (L, 1);
  lua_rawseti (L, -2, CLOSURE_ENV_CALLINFO);
  lua_setuservalue (L, -2);
  lua_rawgetp (L, LUA_REGISTRYINDEX, &closure_mt);
  lua_setmetatable (L, -2);

  /* Allocate and fill ffi_closure data. */
  *closure = ffi_closure_alloc (sizeof (Closure), &call_addr);
  (*closure)->L = L;
  (*closure)->state_lock = lgi_state_get_lock (L);
  (*closure)->call_info = lua_touserdata (L, 1);
  if (ffi_prep_closure_loc (&(*closure)->closure, &(*closure)->call_info->cif,
			    closure_callback, *closure, call_addr) != FFI_OK)
    return luaL_error (L, "Failed to create ffi_prep_closure_loc()");

  /* Create entry in closure_index. */
  lua_rawgetp (L, LUA_REGISTRYINDEX, &closure_index);
  lua_pushvalue (L, -2);
  lua_rawsetp (L, -2, *closure);
  lua_pop (L, 1);

  lua_pushlightuserdata (L, call_addr);
  return 2;
}

/* Callable module public API table. */
static const luaL_Reg call_api_reg[] = {
  { "new", call_new },
  { "toc", call_toc },
  { "tolua", call_tolua },
  { NULL, NULL }
};

void
lgi_call_init (lua_State *L)
{
  /* Register call_info metatable. */
  lua_newtable (L);
  luaL_setfuncs (L, closure_mt_reg, 0);
  lua_rawsetp (L, LUA_REGISTRYINDEX, &closure_mt);

  /* Create call_info index table. */
  lgi_cache_create (L, &closure_index, "v");

  /* Create public api for callable module. */
  lua_newtable (L);
  luaL_setfuncs (L, call_api_reg, 0);
  lua_setfield (L, -2, "call");
}
