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
	 'get_dependencies'
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

   IInterfaceInfo = getface(
      'GIRepository', nil, 'interface_info_', {
	 'get_n_prerequisites', 'get_prerequisite',
	 'get_n_methods', 'get_method', 'get_n_constants', 'get_constant',
      }),

   IObjectInfo = getface(
      'GIRepository', nil, 'object_info_', {
	 'get_parent', 'get_n_interfaces', 'get_interface',
	 'get_n_methods', 'get_method', 'get_n_constants', 'get_constant',
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

-- Metatable for bitfield tables, resolving arbitrary number to the
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

-- Table containing loaders for various GI types, indexed by
-- gi.IInfoType constants.
local typeloader = {}

-- Loads symbol into the specified package.
local function loadsymbol(package, symbol)
   -- Lookup baseinfo of requested symbol in the repo.
   local info = gi.IRepository.find_by_name(nil, package._info.namespace, 
					    symbol)
   -- Decide according to symbol type what to do.
   local value
   if info then
      if not gi.IBaseInfo.is_deprecated(info) then
	 local type = gi.IBaseInfo.get_type(info)
	 if typeloader[type] then
	    value = typeloader[type](info, package)
	 end
      end
      gi.IBaseInfo.unref(info)

      -- Cache the result.
      package[symbol] = value
   end

   return value
end

typeloader[gi.IInfoType.FUNCTION] =
   function(info, package)
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
   function(info, package)
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
   function(info, package)
      return typeloader.enum(info, enum_mt)
   end

typeloader[gi.IInfoType.FLAGS] =
   function(info, package)
      return typeloader.enum(info, bitfield_mt)
   end

typeloader[gi.IInfoType.INTERFACE] =
   function(info, package)
      -- Load all interface methods.
      local value = {}
      load_n(value, info, gi.IInterfaceInfo.get_n_methods,
	     gi.IInterfaceInfo.get_method, core.get)

      -- Load all prerequisites (i.e. inherited interfaces).
      value._inherits = {}
      load_n(value._inherits, info, gi.IInterfaceInfo.get_n_prerequisites,
	     gi.IInterfaceInfo.get_prerequisite,
	     function(pi)
		return loadsymbol(package, gi.IBaseInfo.get_name(pi))
	     end)

      return value
   end

typeloader[gi.IInfoType.OBJECT] =
   function(info, package)
      local value = {}
      -- Load all object methods.
      load_n(value, info, gi.IObjectInfo.get_n_methods,
	     gi.IObjectInfo.get_method, core.get)

      -- Load all constants.
      load_n(value, info, gi.IObjectInfo.get_n_constants,
	     gi.IObjectInfo.get_constant, core.get)

      -- Load parent object.
      value._inherits = {}
--      local pi = gi.IObjectInfo.get_parent(info)
--      value._inherits._parent = loadsymbol(package, gi.IBaseInfo.get_name(pi))
--      gi.IBaseInfo.unref(pi)

      -- Load implemented interfaces.
      load_n(value._inherits, info, gi.IObjectInfo.get_n_interfaces,
	     gi.IObjectInfo.get_interface,
	     function(pi)
		return loadsymbol(package, gi.IBaseInfo.get_name(pi))
	     end)
      return value
   end

-- Loads package, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
function core.load_package(namespace, version)

   -- Create package table with _info table containing auxiliary information
   -- and data for the package.
   local package = { _info = { namespace = namespace, version = version,
			       dependencies = {} } }

   -- Load the typelibrary for the namespace.
   package._info.typelib = assert(gi.IRepository.require(
				     nil, namespace, version))

   -- Load all package dependencies.
   for _, dep in pairs(gi.IRepository.get_dependencies(nil, namespace)) do
      local name, ver = string.match(dep, '(.+)-(.+)')
      package._info.dependencies[name] = ver
   end

   -- Install 'force' closure, which forces loading this namespace.
   package._info.load = 
      function()
	 -- Iterate through all items in the namespace and dereference them,
	 -- which causes them to be loaded in and cached inside the package
	 -- table.
	 for i = 0, gi.IRepository.get_n_infos(nil, namespace) -1 do
	    local info = gi.IRepository.get_info(nil, namespace, i)
	    pcall(loadsymbol, package, gi.IBaseInfo.get_name(info))
	    gi.IBaseInfo.unref(info)
	 end
      end

   -- _info table serves also as a metatable for the package.
   package._info.__index = loadsymbol
   return setmetatable(package, package._info)
end
