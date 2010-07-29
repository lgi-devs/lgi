--[[--

    Lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, getmetatable, type, pairs, pcall, string, rawget =
      assert, setmetatable, getmetatable, type, pairs, pcall, string, rawget
local bit = require 'bit'
local package = package

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

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

-- Loads symbol from specified compound (object, struct or interface).
-- Recursively looks up inherited elements.
local function find_in_compound(compound, symbol, inherited)
   -- Check fields of this compound.
   for name, container in pairs(compound) do
      if name ~= 0 and name ~= '_inherits' and
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

-- Namespace table for GIRepository, populated with basic methods
-- manually.  Later it will be converted to full-featured repo namespace.
local gi = setmetatable({}, compound_mt)
core.repo.GIRepository = gi

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
   function(namespace, info)
      return core.get(info), '_functions'
   end

typeloader[gi.IInfoType.CONSTANT] =
   function(namespace, info)
      return core.get(info), '_constants'
   end

-- Installs _meta table into given compound according to specified info.
local function add_compound_meta(compound, info)
   compound[0] = compound[0] or {}
   compound[0].type = gi.base_info_get_type(info)
   compound[0].name = gi.base_info_get_namespace(info) .. '.' ..
   gi.base_info_get_name(info)
end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   for i = 0, gi.enum_info_get_n_values(info) - 1 do
      local mi = gi.enum_info_get_value(info, i)
      local name = string.upper(gi.base_info_get_name(mi))
      value[name] = gi.value_info_get_value(mi)
   end

   -- Install _meta table.
   add_compound_meta(value, info)

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

typeloader[gi.IInfoType.ENUM] =
   function(namespace, info)
      return load_enum(info, enum_mt), '_enums'
   end

typeloader[gi.IInfoType.FLAGS] =
   function(namespace, info)
      return load_enum(info, bitflags_mt), '_enums'
   end

-- Loads all fields, consts, properties, methods and interfaces of given
-- object.
local function load_compound(into, info, loads)
   setmetatable(into, compound_mt)
   add_compound_meta(into, info)
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
local function load_struct(namespace, into, info)
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
      local name = namespace[0].name .. '.' .. gi.base_info_get_name(info)
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
   function(namespace, info)
      local value = {}
      load_struct(namespace, value, info)
      return value, '_structs'
   end

typeloader[gi.IInfoType.INTERFACE] =
   function(namespace, info)
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
			     c[ns .. '.' .. n] = repo[ns][n]
			  end }
	 })
      return value, '_interfaces'
   end

-- Loads structure information into table representing the structure
local function load_class(namespace, into, info)
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
			  c[ns .. '.' .. n] = repo[ns][n]
		       end }
      })

   -- Add parent (if any) into _inherits table.
   local parent = gi.object_info_get_parent(info)
   if parent then
      local ns, name = gi.base_info_get_namespace(parent),
      gi.base_info_get_name(parent)
      into._inherits = into._inherits or {}
      into._inherits[ns .. '.' .. name] = repo[ns][name]
   end
end

typeloader[gi.IInfoType.OBJECT] =
   function(namespace, info)
      local value = {}
      load_class(namespace, value, info)
      return value, '_classes'
   end

-- Gets symbol of the specified namespace, if not present yet, tries to load it
-- on-demand.
local function get_symbol(namespace, symbol)
   -- Check, whether symbol is already loaded.
   local value = find_in_compound(namespace, symbol)
   if value then return value end

   -- Lookup baseinfo of requested symbol in the repo.
   local info = gi.IRepository.find_by_name(nil, namespace[0].name, symbol)

   -- Decide according to symbol type what to do.
   if info and not gi.base_info_is_deprecated(info) then
      local infotype = gi.base_info_get_type(info)
      local loader = typeloader[infotype]
      if loader then
	 local category
	 value, category = assert(loader(namespace, info))

	 -- Cache the symbol in specified category in the namespace.
	 namespace[category] = namespace[category] or {}
	 namespace[category][symbol] = value
      end
   end

   return value
end

-- Loads namespace, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
local function load_namespace(namespace, name, version)
   -- If package does not exist yet, create and store it into packages.
   if not namespace then
      namespace = {}
      repo[name] = namespace
   end

   -- Create _meta table containing auxiliary information
   -- and data for the namespace.  This table also serves as metatable for the
   -- namespace, providing __index method for retrieveing namespace content.
   namespace[0] = { name = name, type = 'NAMESPACE',
		       dependencies = {}, __index = get_symbol }
   setmetatable(namespace, namespace[0])

   -- Load the typelibrary for the namespace.
   namespace[0].typelib = assert(gi.IRepository.require(
				       nil, name, version))
   namespace[0].version = version or gi.IRepository.get_version(nil, name)

   -- Load all namespace dependencies.
   for _, dep in pairs(gi.IRepository.get_dependencies(nil, name) or {}) do
      local name, version  = string.match(dep, '(.+)-(.+)')
      namespace[0].dependencies[name] = load_namespace(nil, name, version)
   end

   -- Install 'resolve' closure, which forces loading this namespace.
   -- Useful when someone wants to inspect what's inside (e.g. some
   -- kind of source browser or smart editor).
   namespace[0].resolve =
      function()
	 -- Iterate through all items in the namespace and dereference them,
	 -- which causes them to be loaded in and cached inside the namespace
	 -- table.
	 for i = 0, gi.IRepository.get_n_infos(nil, name) - 1 do
	    local info = gi.IRepository.get_info(nil, name, i)
	    pcall(get_symbol, namespace, gi.base_info_get_name(info))
	 end
      end
   return namespace
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(repo, name)
				      return load_namespace(nil, name)
				   end })

-- Convert our poor-man's GIRepository namespace into full-featured one.
gi._enums.IInfoType = nil
load_namespace(gi, 'GIRepository')
load_class(gi, gi._classes.IRepository,
	   gi.IRepository.find_by_name(nil, gi[0].name, 'IRepository'))

-- Install new loader which will load lgi packages on-demand using 'repo'
-- table.
package.loaders[#package.loaders + 1] =
   function(name)
      local prefix, name = string.match(name, '(.-)%.(.+)')
      if prefix == 'lgi' then
	 local ok, result = pcall(function() return repo[name] end)
	 if not ok or not result then return result end
	 return function() return result end
      end
   end
