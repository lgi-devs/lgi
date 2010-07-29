--[[--

    Lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, getmetatable, type, pairs, pcall, string, rawget =
      assert, setmetatable, getmetatable, type, pairs, pcall, string, rawget
local bit = require 'bit'
local lua_package = package

-- Require core lgi utilities, used during bootstrap.
local core = require 'lgi._core'

module 'lgi'

-- Initial bootstrap phase, We have to set up proper dispose handler
-- for IBaseInfo records, otherwise the rest of this bootstrap code
-- will leak them.
do
   local unref_info = assert(core.find('base_info_unref'))
   local unref = core.get(unref_info)
   core.dispose['GIRepository.IBaseInfo'] = unref;

   -- Note that this is the only place when we need to explicitely
   -- unref any IBaseInfo, because unref_info was created *before*
   -- core.dispose contained unref handler for it.
   unref(unref_info)

   -- Since now any IBaseInfo record is automatically unrefed in its
   -- __gc metamethod.

   -- Make sure that Typelib structure is also properly freed when
   -- allocated (by bootstrap code).
   core.dispose['GIRepository.Typelib'] =
      core.get(assert(core.find('free', 'Typelib')))
end

-- Table with all loaded packages.  Its metatable takes care of loading
-- on-demand.  Created by C-side bootstrap.
local packages = core.packages

-- Loads symbol from specified compound (object, struct or interface).
-- Recursively looks up inherited elements.
local function find_in_compound(compound, symbol, inherited)
   -- Check fields of this compound.
   for name, container in pairs(compound) do
      if name ~= '_meta' and name ~= '_inherits' and 
	 (not inherited or name ~= '_fields') then
	 local val = container[symbol]
	 if val then return val end
      end
   end

   -- Check all inherited compounds.
   for _, inherited in pairs(rawget(compound, '_inherits') or {}) do
      local val = find_in_compound(inherited, symbol, true)
      if val then return val end
   end
end

-- Metatable for compound repo objects.
local compound_mt = { __index = find_in_compound }

-- Metatable for bitflags tables, resolving arbitrary number to the
-- table containing symbolic names of contained bits.
local bitflags_mt = {}
function bitflags_mt.__index(bitflags, value)
   local t = {}
   for name, flag in pairs(bitflags) do
      if type(flag) == 'number' and bit.band(flag, value) == flag then
	 t[flag] = name
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

-- Package table for GIRepository, populated with basic methods
-- manually.  Later it will be converted to full-featured package.
local gi = setmetatable({}, compound_mt)
core.packages.GIRepository = gi

-- Loads given set of symbols into table.
local function get_symbols(into, symbols, container)
   for _, symbol in pairs(symbols) do
      into[symbol] = core.get(assert(core.find(symbol, container)))
   end
end

gi._enums = {}
gi._enums.IInfoType = setmetatable(
   {
      FUNCTION = 1,
      STRUCT = 3,
      ENUM = 5,
      FLAGS = 6,
      OBJECT = 7,
      INTERFACE = 8,
      CONSTANT = 9,
   }, enum_mt)

gi._classes = {}
gi._classes.IRepository = setmetatable({ _methods = {} }, compound_mt)
get_symbols(gi._classes.IRepository._methods, 
	    { 'require', 'find_by_name', 'get_n_infos',
	      'get_info', 'get_dependencies',
	      'get_version', }, 'IRepository')
gi._methods = {}
get_symbols(
   gi._methods, {
      'base_info_get_type', 'base_info_is_deprecated',
      'base_info_get_name', 'base_info_get_namespace',
      'enum_info_get_n_values', 'enum_info_get_value',
      'value_info_get_value',
      'struct_info_is_gtype_struct',
      'struct_info_get_n_fields', 'struct_info_get_field',
      'struct_info_get_n_methods', 'struct_info_get_method',
      'interface_info_get_n_prerequisites', 'interface_info_get_prerequisite',
      'interface_info_get_n_methods', 'interface_info_get_method',
      'interface_info_get_n_constants', 'interface_info_get_constant',
      'interface_info_get_n_properties', 'interface_info_get_property',
      'interface_info_get_n_signals', 'interface_info_get_signal',
      'object_info_get_parent',
      'object_info_get_n_interfaces', 'object_info_get_interface',
      'object_info_get_n_fields', 'object_info_get_field',
      'object_info_get_n_methods', 'object_info_get_method',
      'object_info_get_n_constants', 'object_info_get_constant',
      'object_info_get_n_properties', 'object_info_get_property',
      'object_info_get_n_signals', 'object_info_get_signal',
      })

-- Table containing loaders for various GI types, indexed by
-- gi.IInfoType constants.
local typeloader = {}

typeloader[gi.IInfoType.FUNCTION] =
   function(package, info)
      return core.get(info), '_functions'
   end

typeloader[gi.IInfoType.CONSTANT] =
   function(package, info)
      return core.get(info), '_constants'
   end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   for i = 0, gi.enum_info_get_n_values(info) - 1 do
	    local mi = gi.enum_info_get_value(info, i)
	    value[string.upper(gi.base_info_get_name(mi))] =
	    gi.value_info_get_value(mi)
	 end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

typeloader[gi.IInfoType.ENUM] =
   function(package, info)
      return load_enum(info, enum_mt), '_enums'
   end

typeloader[gi.IInfoType.FLAGS] =
   function(package, info)
      return load_enum(info, bitflags_mt), '_enums'
   end

-- Loads all fields, consts, properties, methods and interfaces of given
-- object.
local function load_compound(into, info, loads)
   setmetatable(into, compound_mt)
   for name, gets in pairs(loads) do
      for i = 0, gets[1](info) - 1 do
	 into[name] = into[name] or {}
	 local mi = gets[2](info, i)
	 local process = gets[3] or function(c, n, i) c[n] = i end
	 process(into[name], gi.base_info_get_name(mi), mi)
      end
   end
end

-- Loads structure information into table representing the structure
local function load_struct(package, into, info)
   -- Avoid exposing internal structs created for object implementations.
   if not gi.struct_info_is_gtype_struct(info) then
      load_compound(
	 into, info,
	 {
	    _methods = { gi.struct_info_get_n_methods, 
			 gi.struct_info_get_method,
			 function(c, n, i) c[n] = core.get(i) end },
	    _fields = { gi.struct_info_get_n_fields,
			gi.struct_info_get_field },
	 })

      -- Try to find dispose method. Unfortunately, there seems to
      -- be no systematic approach in typelibs, so we go for
      -- heuristics; prefer 'unref', then 'free'.  If it does not
      -- fit, specific package has to repair setting in its
      -- postprocessing hook.
      local name = package._meta.name .. '.' .. gi.base_info_get_name(info)
      local dispose = core.dispose[name] or into._methods[1]
      if not dispose then
	 for _, dispname in pairs { 'unref', 'free' } do
	    dispose = into._methods[dispname]
	    if dispose then into._methods[dispname] = nil break end
	 end
	 into._methods[1] = dispose
	 core.dispose[name] = dispose
      end
   end
end

typeloader[gi.IInfoType.STRUCT] =
   function(package, info)
      local value = {}
      load_struct(package, value, info)
      return value, '_structs'
   end

typeloader[gi.IInfoType.INTERFACE] =
   function(package, info)
      -- Load all components of the interface.
      local value = {}
      load_compound(
	 value, info,
	 {
	    _properties = { gi.interface_info_get_n_properties,
			    gi.interface_info_get_property,
			    function(c, n, i) 
			       c[string.gsub(n, '%-', '_')] = i
			    end },
	    _methods = { gi.interface_info_get_n_methods,
			 gi.interface_info_get_method,
			 function(c, n, i) c[n] = core.get(i) end },
	    _signals = { gi.interface_info_get_n_signals,
			 gi.interface_info_get_signal },
	    _constants = { gi.interface_info_get_n_constants,
			   gi.interface_info_get_constant,
			   function(c, n, i) c[n] = core.get(i) end },
	    _inherits = { gi.interface_info_get_n_prerequisites,
			  gi.interface_info_get_prerequisite,
			  function(c, n, i)
			     local ns = gi.base_info_get_namespace(i)
			     c[ns .. '.' .. n] = packages[ns][n]
			  end }
	 })
      return value, '_interfaces'
   end

-- Loads structure information into table representing the structure
local function load_class(package, into, info)
   -- Load components of the object.
   load_compound(
      into, info,
      {
	 _properties = { gi.object_info_get_n_properties,
			 gi.object_info_get_property,
			 function(c, n, i) 
			    c[string.gsub(n, '%-', '_')] = i
			 end },
	 _methods = { gi.object_info_get_n_methods,
		      gi.object_info_get_method,
		      function(c, n, i) c[n] = core.get(i) end },
	 _signals = { gi.object_info_get_n_signals,
		      gi.object_info_get_signal },
	 _constants = { gi.object_info_get_n_constants,
			gi.object_info_get_constant,
			function(c, n, i) c[n] = core.get(i) end },
	 _inherits = { gi.object_info_get_n_interfaces,
		       gi.object_info_get_interface,
		       function(c, n, i)
			  local ns = gi.base_info_get_namespace(i)
			  c[ns .. '.' .. n] = packages[ns][n]
		       end }
      })

   -- Add parent (if any) into _inherits table.
   local parent = gi.object_info_get_parent(info)
   if parent then
      local ns, name = gi.base_info_get_namespace(parent), 
      gi.base_info_get_name(parent)
      into._inherits = into._inherits or {}
      into._inherits[ns .. '.' .. name] = packages[ns][name]
   end
end

typeloader[gi.IInfoType.OBJECT] =
   function(package, info)
      local value = {}
      load_class(package, value, info)
      return value, '_classes'
   end

-- Gets symbol of the specified package, if not present yet, tries to load it
-- on-demand.
local function get_symbol(package, symbol)
   -- Check, whether symbol is already loaded.
   local value = find_in_compound(package, symbol)
   if value then return value end

   -- Lookup baseinfo of requested symbol in the repo.
   local info = gi.IRepository.find_by_name(nil, package._meta.name, symbol)

   -- Decide according to symbol type what to do.
   if info and not gi.base_info_is_deprecated(info) then
      local infotype = gi.base_info_get_type(info)
      local loader = typeloader[infotype]
      if loader then
	 local category
	 value, category = assert(loader(package, info))

	 -- Cache the symbol in specified category in the package.
	 package[category] = package[category] or {}
	 package[category][symbol] = value
      end
   end

   return value
end

-- Loads package, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
local function load_package(package, namespace, version)
   -- If package does not exist yet, create and store it into packages.
   if not package then
      package = {}
      packages[namespace] = package
   end

   -- Create _meta table containing auxiliary information
   -- and data for the package.  This table also serves as metatable for the
   -- package, providing __index method for retrieveing namespace content.
   package._meta = { name = namespace, type = 'NAMESPACE', 
		     dependencies = {}, __index = get_symbol }
   setmetatable(package, package._meta)

   -- Load the typelibrary for the namespace.
   package._meta.typelib = assert(gi.IRepository.require(
				     nil, namespace, version))
   package._meta.version = version or
      gi.IRepository.get_version(nil, namespace)

   -- Load all package dependencies.
   for _, dep in pairs(gi.IRepository.get_dependencies(nil, namespace) or {}) do
      local name, version  = string.match(dep, '(.+)-(.+)')
      package._meta.dependencies[name] = load_package(nil, name, version)
   end

   -- Install 'resolve' closure, which forces loading this namespace.
   -- Useful when someone wants to inspect what's inside (e.g. some
   -- kind of source browser or smart editor).
   package._meta.resolve =
      function()
	 -- Iterate through all items in the namespace and dereference them,
	 -- which causes them to be loaded in and cached inside the package
	 -- table.
	 for i = 0, gi.IRepository.get_n_infos(nil, namespace) - 1 do
	    local info = gi.IRepository.get_info(nil, namespace, i)
	    pcall(get_symbol, package, gi.base_info_get_name(info))
	 end
      end
   return package
end

-- Install metatable into packages table, so that on-demand loading works.
setmetatable(packages, { __index = function(packages, name)
				      return load_package(nil, name)
				   end })

-- Convert our poor-man's GIRepository package into full-featured one.
gi._enums.IInfoType = nil
load_package(gi, 'GIRepository')
load_class(gi, gi._classes.IRepository, gi.IRepository.find_by_name(nil, gi._meta.name, 'IRepository'))

-- Install new loader which will load packages on-demand using
-- 'packages' table.
lua_package.loaders[#lua_package.loaders + 1] =
   function(name)
      local prefix, name = string.match(name, '(.+)%.(.+)')
      if prefix == 'lgi' then
	 local ok, result = pcall(load_package, nil, name)
	 if not ok or not result then return result end
	 return function() return result end
      end
   end
