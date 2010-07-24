--[[--

    Base lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, getmetatable, pairs, pcall, string = 
   assert, setmetatable, getmetatable, pairs, pcall, string
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
	 'require', 'find_by_name', 'get_n_infos', 'get_info',
      }),
   IBaseInfo = getface(
      'GIRepository', nil, 'base_info_', {
	 'ref', 'unref', 'get_type', 'is_deprecated', 'get_name',
      }),

   IEnumInfo = getface(
      'GIRepository', nil, 'enum_info_', {
	 'get_n_values', 'get_value',
      }),

   IValueInfo = getface(
      'GIRepository', nil, 'value_info_', {
	 'get_value',
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
   if not gi.IBaseInfo.is_deprecated(info) then
      local type = gi.IBaseInfo.get_type(info)
      if type == gi.INFO_TYPE_FUNCTION or type == gi.INFO_TYPE_CONSTANT then
	 value = core.get(info)
      elseif type == gi.INFO_TYPE_STRUCT then
	 gi.IBaseInfo.ref(info)
	 value = { new = function() return core.get(info) end }
      elseif type == gi.INFO_TYPE_ENUM then
	 value = {}
	 for i = 0, gi.IEnumInfo.get_n_values(info) - 1 do
	    local val = gi.IEnumInfo.get_value(info, i)
	    local n = string.upper(gi.IBaseInfo.get_name(val))
	    local v = gi.IValueInfo.get_value(val)
	    value[n] = v
	    value[v] = n
	    gi.IBaseInfo.unref(val)
	 end
      end
   end

   gi.IBaseInfo.unref(info)

   -- Cache the result.
   package[name] = value
   return value
end

-- Forces loading the whole namespace (which is otherwise loaded
-- lazily).  Useful when one wants to inspect the contents of the
-- whole namespace (i.e. iterate through it).
local function loadnamespace(namespace)
   -- Iterate through all items in the namespace.
   for i = 0, gi.IRepository.get_n_infos(nil, namespace._namespace) -1 do
      local info = gi.IRepository.get_info(nil, namespace._namespace, i)
      pcall(getmetatable(namespace).__index, namespace, 
	    gi.IBaseInfo.get_name(info))
      gi.IBaseInfo.unref(info)
   end
end

function core.require(namespace, version)
   local ns = { _namespace = namespace }

   -- Load the repository.
   ns._typelib = assert(gi.IRepository.require(nil, namespace, version))

   -- Install 'force' closure, which forces loading this namespace.
   ns._force = function() 
		  loadnamespace(ns) 
		  return ns 
	       end

   -- Set proper lazy metatable.
   return setmetatable(ns, package_mt)
end
