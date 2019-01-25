/*
 * LGI regression test.
 * Copyright (c) 2019 Uli Schlachter
 *
 * Licensed under the MIT license:
 * http://www.opensource.org/licenses/mit-license.php
 */

#include <stdio.h>
#include <stdlib.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static void run_string (lua_State *L, const char *str)
{
  int result = luaL_dostring (L, str);
  if (result != 0)
    {
      fprintf (stderr, "Error %d: %s\n", result, lua_tostring (L, -1));
      exit(1);
    }
}

const char add_async[] =
"local lgi = require('lgi');"
"local GLib = lgi.GLib;"
"local Gio = lgi.Gio;"
"local bytes = GLib.Bytes.new('Test', 4);"
"local stream = Gio.MemoryInputStream.new_from_bytes(bytes);"
"Gio.Async.start(function(stream)"
"  assert(stream:async_read_bytes(4):get_data() == 'Test');"
"  done = true;"
"end)(stream)"
;

int main()
{
  /* Set up multiple Lua states */
  lua_State *L1 = luaL_newstate ();
  lua_State *L2 = luaL_newstate ();
  lua_State *L3 = luaL_newstate ();

  luaL_openlibs (L1);
  luaL_openlibs (L2);
  luaL_openlibs (L3);

  /* Prepare so that coroutines in L1 and L2 are resumed by the main loop */
  run_string (L1, add_async);
  run_string (L2, add_async);

  /* Do a main loop iteration */
  run_string (L3, "require('lgi').GLib.MainContext.default():iteration(true)");

  /* Check that both coroutines are done */
  lua_getglobal(L1, "done");
  if (lua_toboolean(L1, -1) == 0) {
    fputs ("Test #1 not finished\n", stderr);
    exit (1);
  }
  lua_getglobal(L2, "done");
  if (lua_toboolean(L2, -1) == 0) {
    fputs ("Test #2 not finished\n", stderr);
    exit (1);
  }

  lua_close (L1);
  lua_close (L2);
  lua_close (L3);

  puts ("Success");
  return 0;
}
