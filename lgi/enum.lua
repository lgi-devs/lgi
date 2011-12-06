------------------------------------------------------------------------------
--
--  LGI Support for enums and bitflags
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local setmetatable, pairs, type = setmetatable, pairs, type
local core = require 'lgi.core'
local gi = core.gi

local enum = {}

function enum.load(info, meta)
   local value = {}

   -- Load all enum values.
   local values = info.values
   for i = 1, #values do
      local mi = values[i]
      value[mi.name:upper()] = mi.value
   end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

-- Enum reverse mapping, value->name.
enum.enum_mt = {}
function enum.enum_mt:__index(value)
   for name, val in pairs(self) do
      if val == value then return name end
   end
end

-- Resolving arbitrary number to the table containing symbolic names
-- of contained bits.
enum.bitflags_mt = {}
function enum.bitflags_mt:__index(value)
   if type(value) ~= 'number' then return end
   local t = {}
   for name, flag in pairs(self) do
      if type(flag) == 'number' and core.has_bit(value, flag) then
	 t[name] = flag
      end
   end
   return t
end

return enum
