--[[--

    Base lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, getmetatable, type, pairs, pcall, string, table =
   assert, setmetatable, getmetatable, type, pairs, pcall, string, table
local core = require 'lgi._core'
local bit = require 'bit'

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

   IStructInfo = getface(
      'GIRepository', nil, 'struct_info_', {
	 'get_n_methods', 'get_method', 'is_gtype_struct',
      }),

   IInfoType = {
      FUNCTION = 1,
      STRUCT = 3,
      ENUM = 5,
      FLAGS = 6,
      OBJECT = 7,
      INTERFACE = 8,
      CONSTANT = 9,
   },
}

-- Expose 'gi' utility in core namespace.
core.gi = gi

-- Metatable for bitfield tables, resolving arbitraru number to the
-- table containing symbolic names of contained bits.
local bitfield_mt = {}
function bitfield_mt.__index(bitfield, value)
   local t = {}
   for name, flag in pairs(bitfield) do
      if type(flag) == 'number' and bit.band(flag, value) == flag then
	 table.insert(t, name)
      end
   end
   return t
end

-- Similar metatable for enum tables.
local enum_mt = {}
function enum_mt.__index(enum, value)
   for name, val in pairs(enum) do
      if val == value then return name end
   end
end

-- Table containing loaders for various GI types.
local typeloader = {}
typeloader[gi.IInfoType.FUNCTION] =
   function(info)
      return core.get(info)
   end

typeloader[gi.IInfoType.CONSTANT] = typeloader[gi.IInfoType.FUNCTION]

local function load_n(t, info, get_n_items, get_item, item_value, transform)
   for i = 0, get_n_items(info) - 1 do
      local mi = get_item(info, i)
      transform = transform or function(val) return val end
      t[transform(gi.IBaseInfo.get_name(mi))] = item_value(mi)
      gi.IBaseInfo.unref(mi)
   end
end

typeloader[gi.IInfoType.STRUCT] =
   function(info)
      local value

      -- Avoid exposing internal structs created for object implementations.
      if not gi.IStructInfo.is_gtype_struct(info) then
	 value = {}

	 -- Create table with all methods of the structure.
	 load_n(value, info, gi.IStructInfo.get_n_methods,
		gi.IStructInfo.get_method, core.get)
      end
      return value
   end

function typeloader.enum(info, meta)
   local value = {}

   -- Load all enum values.
   load_n(value, info, gi.IEnumInfo.get_n_values, gi.IEnumInfo.get_value,
	  gi.IValueInfo.get_value, string.upper)

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

typeloader[gi.IInfoType.ENUM] =
   function(info)
      return typeloader.enum(info, enum_mt)
   end

typeloader[gi.IInfoType.FLAGS] =
   function(info)
      return typeloader.enum(info, bitfield_mt)
   end

-- Loads symbol into the specified package.
local function loadsymbol(package, name)
   -- Lookup baseinfo of requested symbol in the repo.
   local info = gi.IRepository.find_by_name(nil, package._namespace, name)
   if not info then return nil end

   -- Decide according to symbol type what to do.
   local value
   if not gi.IBaseInfo.is_deprecated(info) then
      local type = gi.IBaseInfo.get_type(info)
      if typeloader[type] then
	 value = typeloader[type](info)
      end
   end

   gi.IBaseInfo.unref(info)

   -- Cache the result.
   package[name] = value
   return value
end

-- Package uses lazy namespace access, so __index method loads field
-- on-demand (but stores them back, so it is actually caching).
local package_mt = { __index = loadsymbol }

-- Forces loading the whole namespace (which is otherwise loaded
-- lazily).  Useful when one wants to inspect the contents of the
-- whole namespace (i.e. iterate through it).
local function loadnamespace(namespace)
   -- Iterate through all items in the namespace.
   for i = 0, gi.IRepository.get_n_infos(nil, namespace._namespace) -1 do
      local info = gi.IRepository.get_info(nil, namespace._namespace, i)
      pcall(loadsymbol, namespace, gi.IBaseInfo.get_name(info))
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
