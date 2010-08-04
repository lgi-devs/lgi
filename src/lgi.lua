--[[--

    Lgi bootstrapper.

    Author: Pavel Holejsovsky
    Licence: MIT

--]]--

local assert, setmetatable, getmetatable, type, pairs, pcall, string, rawget,
   table, require, tostring, error, ipairs =
      assert, setmetatable, getmetatable, type, pairs, pcall, string, rawget,
      table, require, tostring, error, ipairs
local bit = require 'bit'
local package = package

-- Require core lgi utilities, used during bootstrap.
local core = require 'lgi._core'

module 'lgi'

-- Prepare logging support.
core.log = core.log or function() end
local function log(format, ...) core.log(format:format(...), 'debug') end
local function loginfo(format, ...) core.log(format:format(...), 'info') end

loginfo 'starting Lgi bootstrap'

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

-- Table with all categories which should be looked up when searching
-- for symbol.
local categories = {
   ['_classes'] = true, ['_structs'] = true, ['_enums'] = true,
   ['_functions'] = true, ['_constants'] = true, ['_callbacks'] = true,
   ['_methods'] = true, ['_fields'] = true, ['_properties'] = true,
   ['_signals'] = true,
}

-- Loads symbol from specified compound (object, struct or interface).
-- Recursively looks up inherited elements.
local function find_in_compound(compound, symbol, inherited)
   -- Check fields of this compound.
   for name, container in pairs(compound) do
      if categories[name] then
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

gi._enums = { IInfoType = setmetatable({
					  FUNCTION = 1,
					  CALLBACK = 2,
					  STRUCT = 3,
					  ENUM = 5,
					  FLAGS = 6,
					  OBJECT = 7,
					  INTERFACE = 8,
					  CONSTANT = 9,
					  TYPE = 18,
				       }, enum_mt),
	      ITypeTag = setmetatable({
					 ARRAY = 15,
					 INTERFACE = 16,
					 GLIST = 17,
					 GSLIST = 18,
				      }, enum_mt),
	      IArrayType = setmetatable({
					   C = 0,
					   ARRAY = 1,
					}, enum_mt),
	      IFunctionInfoFlags = setmetatable({
						   IS_CONSTRUCTOR = 2,
						   IS_GETTER = 4,
						   IS_SETTER = 8
						}, bitflags_mt),
	   }

-- We have to set up proper dispose handler for IBaseInfo and Typelib
-- otherwise the rest of this bootstrap code will leak them.  First of all
-- create metas for IBaseInfo and ITypelib, then look up ref/unref/free
-- handlers (so that find-returned records have already properly assigned
-- metas) and then dereference record and assign acquire/dispose methods.
gi._structs = {
   IBaseInfo = { [0] = { name = "GIRepository.IBaseInfo",
			 type = gi.IInfoType.STRUCT } },
   Typelib = { [0] = { name = "GIRepository.Typelib",
		       type = gi.IInfoType.STRUCT } },
}

gi._structs.IBaseInfo[0].acquire = core.get(core.find('base_info_ref'))
gi._structs.IBaseInfo[0].dispose = core.get(core.find('base_info_unref'))
gi._structs.Typelib[0].dispose = core.get(core.find('free', 'Typelib'))

log 'IBaseInfo and Typelib dispose/acquire installed'

-- Loads given set of symbols into table.
local function get_symbols(into, symbols, container)
   for _, symbol in pairs(symbols) do
      into[symbol] = core.get(assert(core.find(symbol, container)))
   end
end

gi._classes = { IRepository = setmetatable({ _methods = {} }, compound_mt) }
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
      'registered_type_info_get_g_type',
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
      'type_info_get_tag', 'type_info_get_param_type',
      'type_info_get_interface', 'type_info_get_array_type',
      'callable_info_get_return_type', 'callable_info_get_n_args',
      'callable_info_get_arg',
      'function_info_get_flags',
      'arg_info_get_type',
      'constant_info_get_type',
      'property_info_get_type',
      'field_info_get_type',
      })

loginfo 'repo.GIRepository pre-populated'

-- Weak table containing symbols which currently being loaded.	These
-- symbols are not typeinfo-checked to avoid infinite recursion.
local in_load = setmetatable({}, { __mode = 'v' })

-- Checks that type represented by ITypeInfo can be handled.
local function check_type(typeinfo)
   local tag, bi = gi.type_info_get_tag(typeinfo)
   if tag < gi.ITypeTag.ARRAY then return true
   elseif tag == gi.ITypeTag.ARRAY then
      local atype = gi.type_info_get_array_type(typeinfo)
      if atype ~= gi.IArrayType.C and atype ~= gi.IArrayType.ARRAY then
	 error("dependent type array bad type " .. atype)
      end
      bi = gi.type_info_get_param_type(typeinfo, 0)
   elseif tag == gi.ITypeTag.GLIST or tag == gi.ITypeTag.GSLIST then
      bi = gi.type_info_get_param_type(typeinfo, 0)
   elseif tag == gi.ITypeTag.INTERFACE then
      bi = gi.type_info_get_interface(typeinfo)
   end
   assert(bi, "dependent type " .. tag .. " can't be handled")
   local type = gi.base_info_get_type(bi)
   if type == gi.IInfoType.TYPE then
      check_type(bi)
   else
      local ns, name  = gi.base_info_get_namespace(bi),
      gi.base_info_get_name(bi)
      if not in_load[ns .. '.' .. name] then
	 assert(ns and name and repo[ns][name],
		"dependent type `" .. ns .. "." .. name .. "' not available")
      end
   end
end

-- Checks all arguments and return type of specified ICallableInfo.
local function check_callable(info)
   check_type(gi.callable_info_get_return_type(info))
   for i = 0, gi.callable_info_get_n_args(info) - 1 do
      local ai = gi.callable_info_get_arg(info, i)
      check_type(gi.arg_info_get_type(ai))
   end
end

-- Table containing loaders for various GI types, indexed by
-- gi.IInfoType constants.
local typeloader = {}

typeloader[gi.IInfoType.FUNCTION] =
   function(namespace, info)
      check_callable(info)
      return core.get(info), '_functions'
   end

typeloader[gi.IInfoType.CALLBACK] =
   function(namespace, info)
      check_callable(info)
      return info, '_callbacks'
   end

typeloader[gi.IInfoType.CONSTANT] =
   function(namespace, info)
      check_type(gi.constant_info_get_type(info))
      return core.get(info), '_constants'
   end

-- Installs _meta table into given compound according to specified info.
local function add_compound_meta(compound, info)
   compound[0] = compound[0] or {}
   compound[0].type = gi.base_info_get_type(info)
   compound[0].gtype = gi.registered_type_info_get_g_type(info)
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
	 pcall(gets[3], into[name], gi.base_info_get_name(mi), mi)
      end
   end
end

-- Loads structure information into table representing the structure
local function load_struct(namespace, into, info)
   -- Avoid exposing internal structs created for object implementations.
   if not gi.struct_info_is_gtype_struct(info) then
      local has_ctor = false
      load_compound(
	 into, info,
	 {
	    _methods = {
	       gi.struct_info_get_n_methods,
	       gi.struct_info_get_method,
	       function(c, n, i)
		  local flags = gi.function_info_get_flags(i)
		  if bit.band(
		     flags, bit.bor(gi.IFunctionInfoFlags.IS_GETTER,
				    gi.IFunctionInfoFlags.IS_SETTER)) == 0 then
		     check_callable(i)
		     c[n] = core.get(i)
		     if bit.band(
			flags, gi.IFunctionInfoFlags.IS_CONSTRUCTOR) ~= 0 then
			has_ctor = true
		     end
		  end
	       end },
	    _fields = {
	       gi.struct_info_get_n_fields,
	       gi.struct_info_get_field,
	       function(c, n, i)
		  check_type(gi.field_info_get_type(i));
		  c[n] = i
	       end },
	 })

      -- Try to find dispose and acquire methods. If not set otherwise, choose
      -- 'ref' for acquire and 'unref', 'free'.
      local function sethandler(name, alternatives)
	 local handler = into[0][name]
	 if not handler then
	    for _, aname in ipairs(alternatives) do
	       handler = (into._methods or {})[aname]
	       if handler then into._methods[aname] = nil break end
	    end
	    into[0][name] = handler
	 end
      end

      sethandler('acquire', { 'ref' })
      sethandler('dispose', { 'unref', 'free' })

      -- If there is still no dispose handler, assign GLib.g_free() to do it.
      if not into[0].dispose then into[0].dispose = repo.GLib.free end

      -- If there is no constructor, generate one which sets up requested
      -- fields.
      if not has_ctor then
	 into._methods = into._methods or {}
	 into._methods.new = function(fields)
				local struct = core.get(info)
				for field, value in pairs(fields or {}) do
				   struct[field] = value
				end
				return struct
			     end
      end
   end
end

typeloader[gi.IInfoType.STRUCT] =
   function(namespace, info)
      local value = {}
      load_struct(namespace, value, info)
      return value, '_structs'
   end

-- Removes accessor methods for properties.  Properties should be accessed as
-- properties, not by function calls, and this removes some clutter and memory
-- overhead.
local function remove_property_accessors(compound)
   if compound._methods then
      for propname in pairs(compound._properties or {}) do
	 compound._methods['get_' .. propname] = nil
	 compound._methods['set_' .. propname] = nil
      end
   end
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
			       check_type(gi.property_info_get_type(i))
			       c[string.gsub(n, '%-', '_')] = i
			    end },
	    _methods = {
	       gi.interface_info_get_n_methods,
	       gi.interface_info_get_method,
	       function(c, n, i)
		  local flags = gi.function_info_get_flags(i)
		  if bit.band(
		     flags, bit.bor(gi.IFunctionInfoFlags.IS_GETTER,
				    gi.IFunctionInfoFlags.IS_SETTER)) == 0 then
		     check_callable(i)
		     c[n] = core.get(i)
		  end
	       end },
	    _signals = { gi.interface_info_get_n_signals,
			 gi.interface_info_get_signal,
			 function(c, n, i)
			    check_callable(i)
			    c[n] = i
			 end },
	    _constants = { gi.interface_info_get_n_constants,
			   gi.interface_info_get_constant,
			   function(c, n, i)
			      check_type(gi.constant_info_get_type(i))
			      c[n] = core.get(i)
			   end },
	    _inherits = { gi.interface_info_get_n_prerequisites,
			  gi.interface_info_get_prerequisite,
			  function(c, n, i)
			     local ns = gi.base_info_get_namespace(i)
			     c[ns .. '.' .. n] = repo[ns][n]
			  end },
	 })
      remove_property_accessors(value)
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
			    check_type(gi.property_info_get_type(i))
			    c[string.gsub(n, '%-', '_')] = i
			 end },
	 _methods = { gi.object_info_get_n_methods,
		      gi.object_info_get_method,
		      function(c, n, i)
			 check_callable(i)
			 c[n] = core.get(i)
		      end },
	 _signals = { gi.object_info_get_n_signals,
		      gi.object_info_get_signal,
		      function(c, n, i)
			 check_type(gi.constant_info_get_type(i))
			 c[n] = core.get(i)
		      end },
	 _constants = { gi.object_info_get_n_constants,
			gi.object_info_get_constant,
			function(c, n, i)
			   check_type(gi.constant_info_get_type(i))
			   c[n] = core.get(i)
			end },
	 _inherits = { gi.object_info_get_n_interfaces,
		       gi.object_info_get_interface,
		       function(c, n, i)
			  local ns = gi.base_info_get_namespace(i)
			  c[ns .. '.' .. n] = repo[ns][n]
		       end },
      })

   -- Add parent (if any) into _inherits table.
   local parent = gi.object_info_get_parent(info)
   if parent then
      local ns, name = gi.base_info_get_namespace(parent),
      gi.base_info_get_name(parent)
      into._inherits = into._inherits or {}
      into._inherits[ns .. '.' .. name] = repo[ns][name]
   end
   remove_property_accessors(into)
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
   log('loading symbol %s.%s', namespace[0].name, symbol)
   local info = gi.IRepository.find_by_name(nil, namespace[0].name, symbol)

   -- Store the symbol into the in-load table, because we have to
   -- avoid infinte recursion which might happen during type
   -- validation if there are cycles in type definitions (e.g. struct
   -- defining method having as an argument pointer to the struct).
   local fullname = namespace[0].name .. '.' .. symbol
   in_load[fullname] = info

   -- Decide according to symbol type what to do.
   if info and not gi.base_info_is_deprecated(info) then
      local infotype = gi.base_info_get_type(info)
      local loader = typeloader[infotype]
      if loader then
	 local category
	 value, category = assert(loader(namespace, info))

	 -- Process value with the hook, if some hook is installed.
	 local hook = namespace[0].hook
	 if hook then
	    log('passing symbol %s.%s through external hook',
		namespace[0].name, symbol)
	    value = hook(symbol, value)
	 end

	 -- Cache the symbol in specified category in the namespace.
	 if value then
	    namespace[category] = rawget(namespace, category) or {}
	    namespace[category][symbol] = value
	 end
      end
   end

   in_load[fullname] = nil
   return value
end

-- Put GObject and GLib hooks into .preload.
for name, hook in pairs
{
   GObject = {
      Object = function(value)
		  value._methods = {}
	       end,
   },
   GLib = {
      free = true,

      Date = true,
      DateDay = true, DateMonth = true, DateYear = true, DateWeekday = true,
      DateDMY = true,
      DATE_BAD_DAY = true, DATE_BAD_JULIAN = true, DATE_BAD_YEAR = true,
      TimeVal = true, USEC_PER_SEC = true,

      Timer = true,

      MainLoop = true,
      PRIORITY_HIGH = true, PRIORITY_DEFAULT = true, PRIORITY_HIGH_IDLE = true,
      PRIORITY_DEFAULT_IDLE = true, PRIORITY_LOW = true,
      MainContext = true,
      Source = true,

      MarkupParser = true, MarkupParseContext = true,
      MarkupError = true, MARKUP_ERROR = true, MarkupParseFlags = true,

      Checksum = true, ChecksumType = true,

      OptionError = true, OPTION_ERROR = true, OptionContext = true,
      OptionArg = true, OptionFlags = true, OPTION_REMAINING = true,
      OptionEntry = true, OptionGroup = true,
   }
} do package.preload['lgi._core.' .. name] =
   function()
      return {
	 hook = function(symbol, value)
		   local func = hook[symbol]
		   if func then
		      if type(func) == 'function' then func(value) end
		   else
		      log('filtering out %s.%s', name, symbol)
		      value = nil
		   end
		   return value
		end
      }
   end
end

-- Loads namespace, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
local function load_namespace(into, name)
   -- If package does not exist yet, create and store it into packages.
   if not into then
      into = {}
      repo[name] = into
   end

   log('loading namespace %s', name)

   -- Create _meta table containing auxiliary information
   -- and data for the namespace.  This table also serves as metatable for the
   -- namespace, providing __index method for retrieveing namespace content.
   into[0] = { name = name, type = 'NAMESPACE', dependencies = {},
	       __index = get_symbol }
   setmetatable(into, into[0])

   -- Load override into the namespace hook, if the override exists.
   local ok, override = pcall(require, 'lgi._core.' .. name)
   log('attempting to get hook lgi._core.%s: -> %s(%s)', name, tostring(ok),
       tostring(override))
   if ok and override then into[0].hook = override.hook end

   -- Load the typelibrary for the namespace.
   log('requiring namespace %s', name)
   into[0].typelib = assert(gi.IRepository.require(nil, name, nil, 0))
   into[0].version = gi.IRepository.get_version(nil, name)

   -- Load all namespace dependencies.
   for _, name in pairs(gi.IRepository.get_dependencies(nil, name) or {}) do
      log('getting dependency %s', name)
      into[0].dependencies[name] = repo[string.match(name, '(.+)-.+')]
   end

   -- Install 'resolve' closure, which forces loading this namespace.
   -- Useful when someone wants to inspect what's inside (e.g. some
   -- kind of source browser or smart editor).
   into[0].resolve =
      function()
	 -- Iterate through all items in the namespace and dereference them,
	 -- which causes them to be loaded in and cached inside the namespace
	 -- table.
	 for i = 0, gi.IRepository.get_n_infos(nil, name) - 1 do
	    local info = gi.IRepository.get_info(nil, name, i)
	    pcall(get_symbol, into, gi.base_info_get_name(info))
	 end
      end
   log('namespace %s-%s loaded', name, into[0].version)
   return into
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(repo, name)
				  return load_namespace(nil, name)
			       end })

-- Convert our poor-man's GIRepository namespace into full-featured one.
loginfo 'upgrading repo.GIRepository to full-featured namespace'
gi._enums.IInfoType = nil
gi._enums.ITypeTag = nil
gi._enums.IArrayType = nil
gi._enums.IFunctionInfoFlags = nil
load_namespace(gi, 'GIRepository')
load_class(gi, gi._classes.IRepository,
	   gi.IRepository.find_by_name(nil, gi[0].name, 'IRepository'))
load_struct(gi, gi._structs.Typelib,
	    gi.IRepository.find_by_name(nil, gi[0].name, 'Typelib'))

-- Install new loader which will load lgi packages on-demand using 'repo'
-- table.
loginfo 'installing custom Lua package loader'
package.loaders[#package.loaders + 1] =
   function(name)
      local prefix, name = string.match(name, '^(%w+)%.(%w+)$')
      if prefix == 'lgi' then
	 log('lgi loader: trying to load %s.%s', prefix, name)
	 local ok, result = pcall(function() return repo[name] end)
	 if not ok or not result then return result end
	 return function() return result end
      end
   end
