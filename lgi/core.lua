------------------------------------------------------------------------------
--
--  lgi Lua-side core module selector
--
--  Copyright (c) 2010, 2011, 2015 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

-- This module decides what kind of core routines should be loaded.
-- Currently only one implementation exists, standard-Lua C-side
-- implementation, LuaJIT-FFI-based one is planned.
local core = require 'lgi.corelgilua51'

-- Helper methods for converting between CamelCase and uscore_delim
-- names.
function core.uncamel(name)
   return core.downcase(name:gsub('([%l%d])([%u])', '%1_%2'))
end

return core
