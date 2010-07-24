--[[--

    Base lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, pairs, table = assert, setmetatable, pairs, table
local core = require 'lgi._core'

module 'lgi'

local function getface(namespace, object, prefix, funs)
   local t = {}
   for _, fun in pairs(funs) do
      local info = assert(core.find(namespace, object, prefix .. fun))
      t[fun] = core.get(info)
      core.unref(info)
   end
   return t
end

-- Contains gi utilities, used only locally during bootstrap.
local gi = {
   IRepository = getface(
      'GIRepository', 'IRepository', '', {
	 'require', 'find_by_name', 'get_n_infos', 'get_info'
      }),
   IBaseInfo = getface(
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

-- Package uses lazy namespace access, so __index method loads field
-- on-demand (but stores them back, so it is actually caching).
local package_mt = {}
function package_mt.__index(package, name)
   -- Lookup baseinfo of requested symbol in the repo.
   local info = gi.IRepository.find_by_name(nil, package._namespace, name)
   if not info then return nil end

   -- Decide according to symbol type what to do.
   local value
   local type = gi.IBaseInfo.get_type(info)
   if type == gi.INFO_TYPE_FUNCTION or type == gi.INFO_TYPE_CONSTANT then
      value = core.get(info)
   end

   gi.IBaseInfo.unref(info)
   
   -- Cache the result.
   package[name] = value
   return value
end

function core.require(namespace, version)
   local ns = { _namespace = namespace }

   -- Load the repository.
   ns._typelib = assert(gi.IRepository.require(nil, namespace, version))

   -- Set proper lazy metatable.
   return setmetatable(ns, package_mt)
end
