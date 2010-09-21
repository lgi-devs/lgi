/*
 * Dynamic Lua binding to GObject using dynamic gobject-introspection.
 *
 * Copyright (c) 2010 Pavel Holejsovsky
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Code dealing with GLib-specific stuff, mainly GValue marshalling and Lua
 * GClosure handling and implementation.
 */

#include "lgi.h"

void
lgi_value_init (lua_State *L, GValue *val, GITypeInfo *ti)
{
  GITypeTag tag = g_type_info_get_tag (ti);
  switch (tag)
    {
    case GI_TYPE_TAG_VOID:
      g_value_init (val, G_TYPE_NONE);
      break;

#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup, \
		 val_type, val_get, val_set, ffitype)           \
    case tag:                                                   \
      g_value_init (val, val_type);                             \
      break;
#include "decltype.h"

    case GI_TYPE_TAG_INTERFACE:
      {
	GIBaseInfo* ii = g_type_info_get_interface (ti);
	if (GI_IS_REGISTERED_TYPE_INFO (ii))
	  {
	    g_value_init (val, g_registered_type_info_get_g_type (ii));
	    g_base_info_unref (ii);
	  }
	else
	  {
	    int type = g_base_info_get_type (ii);
	    g_base_info_unref (ii);
	    luaL_error (L, "value_init: bad ti.iface.type=%d", type);
	  }
      }
      break;

      /* TODO: Handle arrays. */

    default:
      luaL_error (L, "value_init: bad ti.tag=%d", (int) tag);
    }
}

int
lgi_value_load (lua_State *L, GValue *val, int narg)
{
  gpointer obj;
  int vals;
  GType type = G_VALUE_TYPE (val);

  if (type == G_TYPE_NONE)
    return 0;
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 gtype, val_get, val_set, ffitype)		\
  else if (type == gtype)					\
    {								\
      val_set (val, check (L, narg));				\
      return 1;							\
    }
#define DECLTYPE_KEY_BY_GTYPE
#include "decltype.h"

  /* Handle other cases. */
  switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_ENUM:
      g_value_set_enum (val, luaL_checkinteger (L, narg));
      return 1;

    case G_TYPE_FLAGS:
      g_value_set_flags (val, luaL_checkinteger (L, narg));
      return 1;

    case G_TYPE_OBJECT:
      {
	vals = lgi_compound_get (L, narg, type, &obj, FALSE);
	g_value_set_object (val, obj);
	lua_pop (L, vals);
	return 1;
      }

    case G_TYPE_BOXED:
      {
	vals = lgi_compound_get (L, narg, type, &obj, FALSE);
	g_value_set_boxed (val, obj);
	lua_pop (L, vals);
	return 1;
      }

    default:
      break;
    }

  return luaL_error (L, "g_value_set: no handling of %s(%s))",
		     g_type_name (type),
		     g_type_name (G_TYPE_FUNDAMENTAL (type)));
}

int
lgi_value_store (lua_State *L, const GValue *val)
{
  GType type = G_VALUE_TYPE (val);
  if (type == G_TYPE_NONE)
    return 0;
#define DECLTYPE(tag, ctype, argf, dtor, push, check, opt, dup,	\
		 gtype, val_get, val_set, ffitype)		\
  else if (type == gtype)					\
    {								\
      push (L, val_get (val));					\
      return 1;							\
    }
#define DECLTYPE_KEY_BY_GTYPE
#include "decltype.h"

  /* Handle other cases. */
  switch (G_TYPE_FUNDAMENTAL (type))
    {
    case G_TYPE_ENUM:
      lua_pushinteger (L, g_value_get_enum (val));
      return 1;

    case G_TYPE_FLAGS:
      lua_pushinteger (L, g_value_get_flags (val));
      return 1;

    case G_TYPE_OBJECT:
    case G_TYPE_BOXED:
      {
	GIBaseInfo *bi = g_irepository_find_by_gtype (NULL, type);
	if (bi != NULL)
	  {
	    gpointer obj = GI_IS_OBJECT_INFO (bi) ?
	      g_value_dup_object (val) : g_value_dup_boxed (val);
	    int vals = lgi_compound_create (L, bi, obj, TRUE);
	    g_base_info_unref (bi);
	    return vals;
	  }
	break;
      }

    default:
      break;
    }

  return luaL_error (L, "g_value_get: no handling or  %s(%s)",
		     g_type_name (type),
		     g_type_name (G_TYPE_FUNDAMENTAL (type)));
}

typedef struct _LgiClosure
{
  GClosure closure;

  /* Context in which should be the closure called. */
  lua_State *L;
  int thread_ref;

  /* Reference to target Lua callable, which will be invoked. */
  int target_ref;
} LgiClosure;

static void
lgi_closure_finalize (gpointer notify_data, GClosure *closure)
{
  LgiClosure *c = (LgiClosure *) closure;
  luaL_unref (c->L, LUA_REGISTRYINDEX, c->thread_ref);
  luaL_unref (c->L, LUA_REGISTRYINDEX, c->target_ref);
}

static void
lgi_gclosure_marshal (GClosure *closure, GValue *return_value,
		      guint n_param_values, const GValue *param_values,
		      gpointer invocation_hint, gpointer marshal_data)
{
  LgiClosure *c = (LgiClosure *) closure;
  int vals = 0, res;

  /* Prepare context in which will everything happen. */
  lua_State *L = lgi_get_callback_state (&c->L, &c->thread_ref);
  luaL_checkstack (L, n_param_values + 1, "");

  /* Store target to be invoked. */
  lua_rawgeti (L, LUA_REGISTRYINDEX, c->target_ref);

  /* Push parameters. */
  while (n_param_values--)
    vals += lgi_value_store (L, param_values++);

  /* Invoke the function. */
  res = lua_pcall (L, vals, 1, 0);
  if (res == 0)
    lgi_value_load (L, return_value, -1);
}

GClosure *
lgi_gclosure_create (lua_State *L, int target)
{
  LgiClosure *c;
  int type = lua_type (L, target);

  /* Check that target is something we can call. */
  if (type != LUA_TFUNCTION && type != LUA_TTABLE && type != LUA_TUSERDATA)
    {
        luaL_typerror (L, target, lua_typename (L, LUA_TFUNCTION));
      return NULL;
    }

  /* Create new closure instance. */
  c = (LgiClosure *) g_closure_new_simple (sizeof (LgiClosure), NULL);

  /* Initialize callback thread to be used. */
  c->L = L;
  lua_pushthread (L);
  c->thread_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Store target into the closure. */
  lua_pushvalue (L, target);
  c->target_ref = luaL_ref (L, LUA_REGISTRYINDEX);

  /* Set marshaller for the closure. */
  g_closure_set_marshal (&c->closure, lgi_gclosure_marshal);

  /* Add destruction notifier. */
  g_closure_add_finalize_notifier (&c->closure, NULL, lgi_closure_finalize);

  /* Remove floating ref from the closure, it is useless for us. */
  g_closure_ref (&c->closure);
  g_closure_sink (&c->closure);
  return &c->closure;
}
