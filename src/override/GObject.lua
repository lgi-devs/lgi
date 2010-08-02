--[[--

    lgi.override.GObject

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local lgi = require 'lgi'
local core = lgi._core

module 'lgi.override.GObject'

-- Table of hook functions for all hooked symbols.
local hooks = {}

-- Hooks Value structure.  Do not leave it out, but remember for
-- internal purposes.
local Value
function hooks.Value(v)
   -- unset() works as dispose handler.
   v[0].dispose = v.unset
   v.unset = nil
   Value = v
   return nil
end

-- Hooks basic Object.
function hooks.Object(o)
   -- Remember important raw methods.

   -- Install wrapper methods.

   -- Remove raw methods.
   o.set_property = nil
   o.get_property = nil
   return o
end

-- Exported hook function.
function hook(symbol, value)
   local f = hooks[symbol]
   if f then value = f(value) end
   return value
end
