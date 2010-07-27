--[[--

    Base lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, getmetatable, type, pairs, pcall, string, table,
   rawget =
      assert, setmetatable, getmetatable, type, pairs, pcall, string, table,
      rawget
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
	 'get_dependencies', 'get_version',
      }),
   IBaseInfo = getface(
      'GIRepository', nil, 'base_info_', {
	 'ref', 'unref', 'get_type', 'is_deprecated', 'get_name',
	 'get_namespace',
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

-- Table with all loaded packages.  Its metatable takes care of loading
-- on-demand.
local packages = {}

-- Metatable for bitflags tables, resolving arbitrary number to the
-- table containing symbolic names of contained bits.
local bitflags_mt = {}
function bitflags_mt.__index(bitflags, value)
   local t = {}
   for name, flag in pairs(bitflags) do
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
local function load_symbol(package, symbol)
   -- Lookup baseinfo of requested symbol in the repo.
   local info = gi.IRepository.find_by_name(nil, package._info.namespace,
					    symbol)
   -- Decide according to symbol type what to do.
   local value
   if info then
      if not gi.IBaseInfo.is_deprecated(info) then
	 local type = gi.IBaseInfo.get_type(info)
	 if typeloader[type] then
	    value = typeloader[type](package, info)
	 end
      end
      gi.IBaseInfo.unref(info)

      -- Cache the result.
      package[symbol] = value
   end

   return value
end

typeloader[gi.IInfoType.FUNCTION] =
   function(package, info)
      return core.get(info)
   end

typeloader[gi.IInfoType.CONSTANT] =
   function(package, info)
      return core.get(info)
   end

typeloader[gi.IInfoType.STRUCT] =
   function(package, info)
      local value

      -- Avoid exposing internal structs created for object implementations.
      if not gi.IStructInfo.is_gtype_struct(info) then
	 value = {}

	 -- Create table with all methods of the structure.
	 for i = 0, gi.IStructInfo.get_n_methods(info) - 1 do
	    local mi = gi.IStructInfo.get_method(info, i)
	    value[gi.IBaseInfo.get_name(mi)] = core.get(mi)
	    gi.IBaseInfo.unref(mi)
	 end
      end
      return value
   end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   for i = 0, gi.IEnumInfo.get_n_values(info) - 1 do
	    local mi = gi.IEnumInfo.get_value(info, i)
	    value[string.upper(gi.IBaseInfo.get_name(mi))] =
	    gi.IValueInfo.get_value(mi)
	    gi.IBaseInfo.unref(mi)
	 end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

typeloader[gi.IInfoType.ENUM] =
   function(package, info)
      return load_enum(info, enum_mt)
   end

typeloader[gi.IInfoType.FLAGS] =
   function(package, info)
      return load_enum(info, bitflags_mt)
   end

local function load_by_info(into, package, info)
   local name = gi.IBaseInfo.get_name(info)
   local namespace = gi.IBaseInfo.get_namespace(info)
   local target_name, value
   if namespace == package._info.namespace then
      target_name = name
      value = package[name]
   else
      target_name = namespace .. '.' .. name
      value = packages[namespace][name]
   end
   into[target_name] = value
end

typeloader[gi.IInfoType.INTERFACE] =
   function(package, info)
      -- Load all interface methods.
      local value = {}
      for i = 0, gi.IInterfaceInfo.get_n_methods(info) - 1 do
	 local mi = gi.IInterfaceInfo.get_method(info, i)
	 value[gi.IBaseInfo.get_name(mi)] = core.get(mi)
	 gi.IBaseInfo.unref(mi)
      end

      -- Load all prerequisites (i.e. inherited interfaces).
      value._inherits = {}
      for i = 0, gi.IInterfaceInfo.get_n_prerequisites(info) - 1 do
	 local pi = gi.IInterfaceInfo.get_prerequisite(info, i)
	 load_by_info(value._inherits, package, pi)
	 gi.IBaseInfo.unref(pi)
      end

      return value
   end

typeloader[gi.IInfoType.OBJECT] =
   function(package, info)
      local value = {}
      -- Load all object methods.
      for i = 0, gi.IObjectInfo.get_n_methods(info) - 1 do
	 local mi = gi.IObjectInfo.get_method(info, i)
	 value[gi.IBaseInfo.get_name(mi)] = core.get(mi)
	 gi.IBaseInfo.unref(mi)
      end

      -- Load all constants.
      for i = 0, gi.IObjectInfo.get_n_constants(info) - 1 do
	 local mi = gi.IObjectInfo.get_constant(info, i)
	 value[gi.IBaseInfo.get_name(mi)] = core.get(mi)
	 gi.IBaseInfo.unref(mi)
      end

      -- Load parent object.
      value._inherits = {}
      local pi = gi.IObjectInfo.get_parent(info)
      if pi then
	 load_by_info(value._inherits, package, pi)
	 gi.IBaseInfo.unref(pi)
      end

      -- Load implemented interfaces.
      for i = 0, gi.IObjectInfo.get_n_interfaces(info) - 1 do
	 local ii = gi.IObjectInfo.get_interface(info, i)
	 load_by_info(value._inherits, package, ii)
	 gi.IBaseInfo.unref(ii)
      end
      return value
   end

-- Loads package, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
local function load_package(packages, namespace, version)

   -- If the package is already loaded, just use it.
   local package = rawget(packages, namespace)
   if package then return package end

   -- Create package table with _info table containing auxiliary information
   -- and data for the package.
   package = { _info = { namespace = namespace, dependencies = {} } }
   packages[namespace] = package

   -- Load the typelibrary for the namespace.
   package._info.typelib = assert(gi.IRepository.require(
				     nil, namespace, version))
   package._info.version = version or
      gi.IRepository.get_version(nil, namespace)

   -- Load all package dependencies.
   for _, dep in pairs(gi.IRepository.get_dependencies(nil, namespace) or {}) do
      local name, version  = string.match(dep, '(.+)-(.+)')
      package._info.dependencies[name] = load_package(packages, name, version)
   end

   -- Install 'resolve' closure, which forces loading this namespace.
   -- Useful when someone wants to inspect what's inside (e.g. some
   -- kind of source browser or smart editor).
   package._info.resolve =
      function()
	 -- Iterate through all items in the namespace and dereference them,
	 -- which causes them to be loaded in and cached inside the package
	 -- table.
	 for i = 0, gi.IRepository.get_n_infos(nil, namespace) -1 do
	    local info = gi.IRepository.get_info(nil, namespace, i)
	    pcall(load_symbol, package, gi.IBaseInfo.get_name(info))
	    gi.IBaseInfo.unref(info)
	 end
      end

   -- _info table serves also as a metatable for the package.
   package._info.__index = load_symbol
   return setmetatable(package, package._info)
end

-- Install metatable into packages table, so that on-demand loading works.
setmetatable(packages, { __index = load_package })

-- Expose 'gi' utility and 'packages' table in core namespace, mostly
-- for debugging purposes.
core.gi = gi
core.packages = packages
