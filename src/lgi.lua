--[[--

    Base lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local pairs, table = pairs, table
local core = require 'lgi._core'

module 'lgi'

-- Helper for loading gi methods used only during bootstrapping.
local function getface(namespace, interface, prefix, functions)
   local t = {}
   for _, func in pairs(functions) do
      local fname = prefix .. func
      t[func] = interface and 
	 core.get(namespace, interface, fname) or core.get(namespace, fname)
   end
   return t
end

-- Contains gi utilities.
local gi = {
   IRepository = loadface(
      'GIRepository', 'IRepository', '', {
	 'require', 'find_by_name', 'get_n_infos', 'get_info'
      }),
   IBaseInfo = loadface(
      'GIRepository', nil, 'base_info_', {
	 'unref', 'get_type', 'get_name', 'is_deprecated', 'get_container',
      }),

   INFO_TYPE_FUNCTION = 1,
   INFO_TYPE_STRUCT = 3,
   INFO_TYPE_ENUM = 5,
   INFO_TYPE_OBJECT = 7,
   INFO_TYPE_INTERFACE = 8,
   INFO_TYPE_CONSTANT = 9,
}

local function load_baseinfo(target, info)
   -- Decide according to type.
   local type = gi.IBaseInfo.get_type(info)
   local name = gi.IBaseInfo.get_name(info)
   if type == gi.INFO_TYPE_CONSTANT or type == gi.INFO_TYPE_FUNCTION then
      target[name] = core.get(info)
   end
end

local ns_mt = {
   -- Tries to lookup symbol in all _enums in the namespace table.
   __index = function(ns, symbol)
		for _, enum in ns._enums do
		   local value = enum[symbol]
		   if value then return value end
		end
	     end,
}

-- Creates namespace table bound to specified glib namespace.
function core.new_namespace(name)
   local ns = { _enums = {}, _name = name }

   -- Recursively populate namespace using GI.
   gi.IRepository.require(nil, name);
   for i = 0, gi.IRepository.get_n_infos(nil) do
      local info = gi.IRepository.get_info(nil, i)
      load_baseinfo(ns, ns, info)
      gi.IBaseInfo.unref(info)
   end

   -- Make sure that namespace table properly resolves unions.
   return setmetatable(ns, ns_mt)
end
