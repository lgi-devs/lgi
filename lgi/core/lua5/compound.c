/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2012 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Management of compounds (objects, structures and unions).
 */

#include "lgi.h"

/* lightuserdata key to LUA_REGISTRYINDEX containing metatable for
   compound. */
static int compound_mt;

/* lightuserdata key to compound.typeof() method. */
static int compound_typeof;

/* Invokes control function of given name in compound's typetable.
   Returns TRUE if such function exists and was called.  Function can
   be either lightuserdata with pointer to native C function with
   'void func(gpointer)' signature, or Lua function with
   'function(typetable, compound)' signature. */
static gboolean
compound_control (lua_State *L, LgiAggregate *compound, int narg,
		  const char *name)
{
  gboolean result = FALSE;
  int type;

  luaL_checkstack (L, 3, NULL);
  narg = lua_absindex (L, narg);
  lua_getuservalue (L, narg);
  lua_getfield (L, -1, name);
  type = lua_type (L, -1);
  if (type == LUA_TNIL)
    lua_pop (L, 2);
  else if (type == LUA_TLIGHTUSERDATA)
    {
      void (*function)(gpointer) = lua_touserdata (L, -1);
      function (compound->addr);
      lua_pop (L, 2);
    }
  else
    {
      lua_pushvalue (L, -2);
      lua_pushvalue (L, narg);
      lua_call (L, 2, 1);
      result = lua_isnone (L, -1) || lua_toboolean (L, -1);
      lua_pop (L, 2);
    }

  return result;
}

/* Similar to lgi_aggr_get(), but throws error in case of the failure. */
static LgiAggregate *
compound_check (lua_State *L, int narg)
{
  LgiAggregate *compound = lgi_aggr_get (L, narg, &compound_mt);
  if (G_UNLIKELY (compound == NULL))
    luaL_argerror (L, narg, "compound expected");

  return compound;
}

static int
compound_gc (lua_State *L)
{
  LgiAggregate *compound = compound_check (L, 1);
  if (compound->owned)
    {
      if (G_UNLIKELY (!compound_control (L, compound, 1, "_unref")))
	{
	  lua_getfield (L, -1, "_name");
	  g_warning ("compound_gc(%s) leaking", lua_tostring (L, -1));
	}
    }
  else if (compound->is_inline)
    /* Inline compound, it might need to deinitialize. */
    compound_control (L, compound, 1, "_deinit");

  return 0;
}

static int
compound_method (lua_State *L)
{
  compound_check (L, 1);

  /* Get method from the typetable (method name is stored in the
     upvalue. */
  lua_getuservalue (L, 1);
  lua_pushvalue (L, lua_upvalueindex (1));
  lua_gettable (L, -2);
  if (lua_isnil (L, -1))
    {
      lua_getfield (L, -3, "_name");
      lua_pushvalue (L, lua_upvalueindex (1));
      return luaL_error (L, "%s: no `_%s'", lua_tostring (L, -2),
			 lua_tostring (L, -1));
    }

  /* Move resulting function to the top of the stack and call it with
     the rest of the args. */
  lua_insert (L, 1);
  lua_insert (L, 2);
  lua_call (L, lua_gettop (L) - 1, LUA_MULTRET);
  return lua_gettop (L);
}

static const struct luaL_Reg compound_mt_reg[] = {
  { "__gc", compound_gc },
  { "__tostring", NULL },
  { "__eq", NULL },
  { "__index", NULL },
  { "__newindex", NULL },
  { "__len", NULL },
  { "__pairs", NULL },
  { "__ipairs", NULL },
  { NULL, NULL }
};

gboolean
lgi_compound_own (lua_State *L, int narg, int action)
{
  LgiAggregate *compound = compound_check (L, narg);
  if (action > 0)
    {
      if (G_UNLIKELY (compound->owned))
	/* Would have double ownership, try to release one
	   reference. */
	return compound_control (L, compound, narg, "_unref");
      else if (G_UNLIKELY (compound->is_inline))
	/* We cannot afford adding ownership to compound which is
	   inline-allocated. */
	return FALSE;

      compound->owned = TRUE;
      return TRUE;
    }
  else
    {
      gboolean succeeded = TRUE;
      if (action < 0)
	{
	  if (G_UNLIKELY (!compound->owned))
	    succeeded = FALSE;

	  compound->owned = FALSE;
	}

      /* If we lost ownership, try to get it back, but only for
	 non-inline allocated compounds. */
      if (!compound->owned && !compound->is_inline)
	if (compound_control (L, compound, narg, "_ref"))
	  compound->owned = TRUE;

      return succeeded;
    }
}

void
lgi_compound_2lua (lua_State *L, int ntypetable, gpointer addr,
		   int owned, int parent)
{
  LgiAggregate *compound;
  int size = 0;

  luaL_checkstack (L, 4, NULL);
  parent = lua_absindex (L, parent);
  ntypetable = lua_absindex (L, ntypetable);

  /* Check, whether the compound is already in the cache. */
  compound = lgi_aggr_find (L, addr, parent);
  if (compound != NULL)
    {
      /* Properly adjust the compound ownership of cached compound. */
      lgi_compound_own (L, -1, owned);
      return;
    }

  /* We avoid creating objects without any typetable. They are
     expected to be found in the cache. */
  g_assert (ntypetable != 0);

  /* Create new userdata proxy for the compound. */
  if (addr == NULL)
    {
      lua_getfield (L, -2, "_size");
      size = lua_tointeger (L, -1);
      if (G_UNLIKELY (size == 0))
	{
	  lua_getfield (L, -3, "_name");
	  luaL_error (L, "cannot instantiate abstract `%s'",
		      lua_tostring (L, -1));
	}

      lua_pop (L, 1);
      owned = 0;
    }

  /* Create and initialize compound contents. */
  compound = lgi_aggr_create (L, &compound_mt, addr, size, parent);
  compound->owned = FALSE;

  /* Temporarily assign compound type.  Will be fine-tuned later,
     when compound.resolve() will be called. */
  if (!lua_isnil (L, ntypetable))
    {
      lua_pushvalue (L, ntypetable);
      lua_setuservalue (L, -2);
    }

  /* Adjust the ownership. */
  if (G_LIKELY (addr != NULL))
    lgi_compound_own (L, -1, owned);

  /* Resolve and set the typetable to be used.  If we get 'nil' as
     original typetable, get runtime GType from the instance
     instead. */
  lua_rawgetp (L, LUA_REGISTRYINDEX, &compound_typeof);
  if (G_UNLIKELY (lua_isnil (L, -1)))
    {
      /* Not set yet, use the type as-is. */
      lua_pop (L, 1);
      lua_pushvalue (L, ntypetable);
    }
  else
    {
      /* Invoke compound.resolve() method to get real type. */
      lua_pushvalue (L, -2);
      if (lua_isnil (L, ntypetable) && addr != NULL)
	lua_pushstring (L, g_type_name (G_TYPE_FROM_INSTANCE (addr)));
      else
	lua_pushvalue (L, ntypetable);
      lua_call (L, 2, 1);
    }

  /* Assign typetable as object's userdata. */
  lua_setuservalue (L, -2);

  /* Inline-allocated elements might need some initialization. */
  if (G_UNLIKELY (addr == NULL))
    compound_control (L, compound, -1, "_init");
}

gpointer
lgi_compound_2c (lua_State *L, int narg, int ntype)
{
  LgiAggregate *compound = lgi_aggr_get (L, narg, &compound_mt);
  if (compound == NULL)
    return NULL;

  if (ntype != 0)
    {
      gboolean is_a;

      /* Check whether type is compatible with our compound. */
      lua_getfield (L, ntype, "_isa");
      lua_pushvalue (L, ntype);
      lua_pushvalue (L, narg);
      lua_call (L, 2, 1);
      is_a = lua_toboolean (L, -1);
      lua_pop (L, 1);
      if (!is_a)
	return NULL;
    }

  return compound->addr;
}

/* Creates new compound or wraps an existing one.
   c = compound.new(typetable[, addr[, owned | parent]]]) */
static int
compound_new (lua_State *L)
{
  int type, parent = 0, owned = 0;
  gpointer addr = NULL;

  luaL_checktype (L, 1, LUA_TTABLE);
  type = lua_type (L, 2);
  if (type != LUA_TNONE && type != LUA_TNIL)
    {
      /* Extract address of the native object. */
      if (type == LUA_TLIGHTUSERDATA)
	addr = lua_touserdata (L, 2);
      else
	addr = GINT_TO_POINTER ((gint) luaL_checknumber (L, 2));

      /* Get ownership flag or parent information. */
      type = lua_type (L, 3);
      if (type == LUA_TBOOLEAN)
	owned = lua_toboolean (L, 3);
      else if (type != LUA_TNONE && type != LUA_TNIL)
	/* We got parent object in arg 3. */
	parent = 3;
    }

  lgi_compound_2lua (L, 1, addr, owned, parent);
  return 1;
}

/* Returns typetable of this compound.
   typetable = compound.type(c) */
static int
compound_type (lua_State *L)
{
  compound_check (L, 1);
  lua_getuservalue (L, 1);
  return 1;
}

/* Returns address as lightuserdata of this compound's instance,
   optionally augmented by specified byte offset.
   addr = compound.addr(c[, offset]) */
static int
compound_addr (lua_State *L)
{
  LgiAggregate *compound = compound_check (L, 1);
  int offset = luaL_optinteger (L, 2, 0);
  lua_pushlightuserdata (L, (char *) compound->addr + offset);
  return 1;
}

/* Sets new typetable or ownership state of the compound.
   compound.set(c, owns)
   - owns: true to give ownership, false to remove it.
   compound.set(c, typetable)
   - typetable: new typetable to assign to this compound. */
static int
compound_set (lua_State *L)
{
  compound_check (L, 1);
  if (lua_istable (L, 2))
    {
      lua_pushvalue (L, 2);
      lua_setfenv (L, 1);
    }
  else
    lgi_compound_own (L, 1, lua_toboolean (L, 2) ? 1 : -1);

  return 0;
}

static const struct luaL_Reg compound_api_reg[] = {
  { "new", compound_new },
  { "type", compound_type },
  { "addr", compound_addr },
  { "set", compound_set },
  { NULL, NULL }
};

static const char *compound_api_options[] = {
  "typeof", NULL
};

static int
compound_api_newindex (lua_State *L)
{
  switch (luaL_checkoption (L, 1, NULL, compound_api_options))
    {
    case 0:
      lua_pushvalue (L, 2);
      lua_rawsetp (L, LUA_REGISTRYINDEX, &compound_typeof);
      break;

    default:
      g_assert_not_reached ();
    }

  return 0;
}

void
lgi_compound_init (lua_State *L)
{
  const luaL_Reg *reg;

  /* Register compound metatable. */
  lua_newtable (L);
  for (reg = compound_mt_reg; reg->name != NULL; reg++)
    {
      lua_pushstring (L, reg->name);
      if (reg->func != NULL)
	lua_pushcfunction (L, reg->func);
      else
	{
	  /* NULL address means that this is compound method,
	     implemented in typetable of the compound.  Create method
	     closure with method name as an upvalue. */
	  lua_pushstring (L, reg->name + 1);
	  lua_pushcclosure (L, compound_method, 1);
	}

      lua_settable (L, -3);
    }
  lua_rawsetp (L, LUA_REGISTRYINDEX, &compound_mt);

  /* Create 'compound' API table in main core API table. */
  lua_newtable (L);
  luaL_setfuncs (L, compound_api_reg);

  /* Create metatable implementing __newindex to be able to store
     important indices set by Lua-side. */
  lua_newtable (L);
  lua_pushcfunction (L, compound_api_newindex);
  lua_setfield (L, -2, "__newindex");
  lua_setmetatable (L, -2);

  /* Store compound API table. */
  lua_setfield (L, -2, "compound");
}
