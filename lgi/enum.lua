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
local component = require 'lgi.component'

local enum = {
   enum_mt = component.mt:clone { '_method' },
   bitflags_mt = component.mt:clone { '_method' }
}

function enum.load(info, meta)
   local enum_type = component.create(info, meta)
   if info.methods then
      enum_type._method = component.get_category(
	 info.methods, core.callable.new)
   else
      -- Enum.methods was added only in GI1.30; for older gi, simulate
      -- the access using lookup in the global namespace.
      local prefix = info.name:gsub('%u+[^%u]+', '%1_'):lower()
      local namespace = core.repo[info.namespace]
      enum_type._method = setmetatable(
	 {}, 
	 { __index = function(_, name) return namespace[prefix .. name] end })
   end

   -- Load all enum values.
   local values = info.values
   for i = 1, #values do
      local mi = values[i]
      enum_type[mi.name:upper()] = mi.value
   end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   return enum_type
end

-- Enum reverse mapping, value->name.
function enum.enum_mt:_element(instance, value)
   local element, category = component.mt._element(self, instance, value)
   if element then return element, category end
   for name, val in pairs(self) do
      if val == value then return name end
   end
end

-- Resolving arbitrary number to the table containing symbolic names
-- of contained bits.
function enum.bitflags_mt:_element(instance, value)
   local element, category = component.mt._element(self, instance, value)
   if element then return element, category end
   if type(value) ~= 'number' then return end
   local result = {}
   for name, flag in pairs(self) do
      if type(flag) == 'number' and core.has_bit(value, flag) then
	 result[name] = flag
      end
   end
   return result
end

return enum
