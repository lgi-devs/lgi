------------------------------------------------------------------------------
--
--  LGI Lua-side lua5 core module selector
--
--  Copyright (c) 2010-2012 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

-- Decide whether runtime is Lua 5.1 or Lua 5.2
if _VERSION == "Lua 5.2" then
   return require 'lgi.core.lua5.lgilua52'
else
   return require 'lgi.core.lua5.lgilua51'
end
