------------------------------------------------------------------------------
--
--  LGI Lua-side core module selector
--
--  Copyright (c) 2010-2012 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

-- This module decides what kind of core routines should be loaded.
-- Currently only one implementation exists, standard-Lua C-side
-- implementation, LuaJIT-FFI-based one is planned.
local core = require 'lgi.core.lua5.lua5'
core.compiler = require 'lgi.core.lua5.compiler'

return core
