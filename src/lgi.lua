--[[--------------------------------------------------------------------------

    Lgi bootstrapper.

    Copyright (c) 2010 Pavel Holejsovsky
    Licensed under the MIT license:
    http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

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

-- Metatable for structs, allowing to 'call' structure, which is
-- translating to creating new structure instance (i.e. constructor).
local struct_mt = { __index = find_in_compound }
function struct_mt.__call(type, fields)
   -- Create the structure instance.
   local info = assert(ri:find_by_gtype(type[0].gtype))
   local struct = core.get(info)

   -- Set values of fields.
   for name, value in pairs(fields or {}) do
      struct[name] = value
   end
   return struct
end

-- Metatable for bitflags tables, resolving arbitrary number to the
-- table containing symbolic names of contained bits.
local bitflags_mt = {}
function bitflags_mt.__index(bitflags, value)
   local t = {}
   for name, flag in pairs(bitflags) do
      if type(flag) == 'number' and bit.band(flag, value) == flag then
	 t[name] = flag
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

-- G_TYPE_NONE constant.
local G_TYPE_NONE = 4 -- (1 << 2)

-- Namespace table for GIRepository, populated with basic methods
-- manually.  Later it will be converted to full-featured repo namespace.
local gi = setmetatable({}, compound_mt)
core.repo.GIRepository = gi
local ir

gi._enums = { InfoType = setmetatable({
					 FUNCTION = 1,
					 CALLBACK = 2,
					 STRUCT = 3,
					 ENUM = 5,
					 FLAGS = 6,
					 OBJECT = 7,
					 INTERFACE = 8,
					 CONSTANT = 9,
					 UNION = 11,
					 SIGNAL = 13,
					 VFUNC = 14,
					 PROPERTY = 15,
					 TYPE = 18,
				      }, enum_mt),
	      TypeTag = setmetatable({
					ARRAY = 15,
					INTERFACE = 16,
					GLIST = 17,
					GSLIST = 18,
					GHASH = 19,
				     }, enum_mt),
	      ArrayType = setmetatable({
					  C = 0,
					  ARRAY = 1,
				       }, enum_mt),
	      FunctionInfoFlags = setmetatable({
						  IS_CONSTRUCTOR = 2,
						  IS_GETTER = 4,
						  IS_SETTER = 8
					       }, bitflags_mt),
	   }

gi._structs = {
   BaseInfo = setmetatable(
      { [0] = { name = 'GIRepository.BaseInfo',
		gtype = core.gtype('BaseInfo') },
	_methods = {}
     }, struct_mt),
   Typelib = setmetatable(
      { [0] = { name = 'GIRepository.Typelib',
		gtype = core.gtype('Typelib') },
	_methods = {}
     }, compound_mt),
}

-- Loads given set of symbols into table.
local function get_symbols(into, symbols, container)
   for _, symbol in pairs(symbols) do
      into[symbol] = core.get(core.find(symbol, container))
   end
end

-- Metatable for interfaces, implementing casting on __call.
local interface_mt = { __index = find_in_compound }
function interface_mt.__call(iface, obj)
   -- Cast operator, 'param' is source object which should be cast.
   local res = iface and core.cast(obj, iface[0].gtype)
   if not res then
      error(string.format("`%s' cannot be cast to `%s'", tostring(obj),
			  iface[0].name));
   end
   return res
end

-- Metatable for classes, again implementing object
-- construction or casting on __call.
local class_mt = { __index = find_in_compound }
function class_mt.__call(class, param)
   local obj
   if type(param) == 'userdata' then
      -- Cast operator, 'param' is source object which should be cast.
      obj = param and core.cast(param, class[0].gtype)
      if not obj then
	 error(string.format("`%s' cannot be cast to `%s'", tostring(param),
			     class[0].name));
      end
   else
      -- Constructor, 'param' contains table with properties/signals to
      -- initialize.
      local params = {}
      local sigs = {}

      -- Get BaseInfo from gtype.
      local info = assert(ir:find_by_gtype(class[0].gtype))

      -- Process 'param' table, create constructor property table and signals
      -- table.
      for name, value in pairs(param or {}) do
	 local info, proptype = class[name]
	 if info then
	    proptype = gi.base_info_get_type(info)
	    if proptype == gi.InfoType.SIGNAL then
	       sigs[name] = value
	    elseif proptype == gi.InfoType.PROPERTY then
	       params[info] = value
	    else
	       info = nil
	    end
	 end
	 if not info then
	    error(string.format("creating '%s': unknown property '%s'",
				class[0].name, name))
	 end
      end

      -- Create the object.
      obj = core.get(info, params)

      -- Attach signals previously filtered out from creation.
      for name, func in pairs(sigs) do obj[name] = func end
   end
   return obj
end

gi._classes = {
   Repository = setmetatable(
      { [0] = { name = 'GIRepository.Repository',
		gtype = core.gtype('Repository')
	     },
	_methods = {}
     }, class_mt),
}
get_symbols(gi._classes.Repository._methods,
	    { 'get_default', 'require', 'find_by_name', 'find_by_gtype',
	      'get_n_infos', 'get_info', 'get_dependencies', 'get_version', },
	    'Repository')
get_symbols(gi._structs.BaseInfo._methods,
	    { 'is_deprecated', 'get_name', 'get_namespace',
	      'get_container' }, 'BaseInfo')
gi._functions = {}
get_symbols(
   gi._functions, {
      'base_info_get_type',
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

-- Remember default repository.
ir = gi.Repository.get_default()

loginfo 'repo.GIRepository pre-populated'

-- Weak table containing symbols which currently being loaded.	These
-- symbols are not typeinfo-checked to avoid infinite recursion.
local in_load = setmetatable({}, { __mode = 'v' })

-- Checks that type represented by 'info' can be handled.
local function check_type(info)
   local type = gi.base_info_get_type(info)
   if type == gi.InfoType.TYPE then
      local tag = gi.type_info_get_tag(info)
      if tag < gi.TypeTag.ARRAY then
	 return true
      elseif tag == gi.TypeTag.ARRAY then
	 -- Array support is still limited in core.
	 local atype = gi.type_info_get_array_type(info)
	 if atype == gi.ArrayType.C or atype == gi.ArrayType.ARRAY then
	    return check_type(gi.type_info_get_param_type(info, 0))
	 end
	 return false
      elseif tag == gi.TypeTag.GLIST or tag == gi.TypeTag.GSLIST then
	 return check_type(gi.type_info_get_param_type(info, 0))
      elseif tag == gi.TypeTag.INTERFACE then
	 return check_type(gi.type_info_get_interface(info))
      elseif tag == gi.TypeTag.GHASH then
	 -- No support for hashtables yet.
	 loginfo('refusing hashtable')
	 return false
      else
	 loginfo('refusing bad typeinfo %d', tag)
	 return false
      end
   elseif type == gi.InfoType.FUNCTION or type == gi.InfoType.CALLBACK or
      type == gi.InfoType.SIGNAL or type == gi.InfoType.VFUNC then
      if not check_type(gi.callable_info_get_return_type(info)) then
	 return false
      end
      for i = 0, gi.callable_info_get_n_args(info) - 1 do
	 local ai = gi.callable_info_get_arg(info, i)
	 if not check_type(gi.arg_info_get_type(ai)) then
	    return false
	 end
      end
   else
      -- Check 'in_load' table.
      if not info:get_container() then
	 local ns, name  = gi.BaseInfo.get_namespace(info),
	 gi.BaseInfo.get_name(info)
	 if not in_load[ns .. '.' .. name] and not repo[ns][name] then
	    loginfo('refusing %s.%s', ns, name)
	    return false
	 end
      end
   end
   return true
end

-- Table containing loaders for various GI types, indexed by
-- gi.InfoType constants.
local typeloader = {}

typeloader[gi.InfoType.FUNCTION] =
   function(namespace, info)
      return check_type(info) and core.get(info), '_functions'
   end

typeloader[gi.InfoType.CALLBACK] =
   function(namespace, info)

      return check_type(info) and info, '_callbacks'
   end

typeloader[gi.InfoType.CONSTANT] =
   function(namespace, info)
      return check_type(gi.constant_info_get_type(info)) and core.get(info),
      '_constants'
   end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   for i = 0, gi.enum_info_get_n_values(info) - 1 do
      local mi = gi.enum_info_get_value(info, i)
      local name = string.upper(gi.BaseInfo.get_name(mi))
      value[name] = gi.value_info_get_value(mi)
   end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

typeloader[gi.InfoType.ENUM] =
   function(namespace, info)
      return load_enum(info, enum_mt), '_enums'
   end

typeloader[gi.InfoType.FLAGS] =
   function(namespace, info)
      return load_enum(info, bitflags_mt), '_enums'
   end

-- Loads all fields, consts, properties, methods and interfaces of given
-- object.
local function load_compound(compound, info, loads, mt)
   -- Fill in meta of the compound.
   compound[0] = compound[0] or {}
   compound[0].gtype = gi.registered_type_info_get_g_type(info)
   compound[0].name = gi.BaseInfo.get_namespace(info) .. '.' ..
   gi.BaseInfo.get_name(info)

   -- Avoid installing tables with constructor into foreign structures.
   setmetatable(compound,
		compound[0].gtype == G_TYPE_NONE and compound_mt or mt)

   -- Iterate and load all groups.
   for name, gets in pairs(loads) do
      for i = 0, gets[1](info) - 1 do
	 compound[name] = rawget(compound, name) or {}
	 local mi = gets[2](info, i)
	 gets[3](compound[name], gi.BaseInfo.get_name(mi), mi)
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
	    _methods = {
	       gi.struct_info_get_n_methods,
	       gi.struct_info_get_method,
	       function(c, n, i)
		  local flags = gi.function_info_get_flags(i)
		  if bit.band(
		     flags, bit.bor(gi.FunctionInfoFlags.IS_GETTER,
				    gi.FunctionInfoFlags.IS_SETTER)) == 0 then
		     if check_type(i) then c[n] = core.get(i) end
		  end
	       end },
	    _fields = {
	       gi.struct_info_get_n_fields,
	       gi.struct_info_get_field,
	       function(c, n, i)
		  if check_type(gi.field_info_get_type(i)) then c[n] = i end
	       end },
	 }, struct_mt)
   end
end

typeloader[gi.InfoType.STRUCT] =
   function(namespace, info)
      local value = {}
      load_struct(namespace, value, info)
      return value, '_structs'
   end

typeloader[gi.InfoType.UNION] =
   function(namespace, info)
      local value = {}
      load_compound(
	 value, info,
	 {
	    _methods = {
	       gi.union_info_get_n_methods,
	       gi.union_info_get_method,
	       function(c, n, i)
		  if check_type(i) then c[n] = core.get(i) end
	       end },
	    _fields = {
	       gi.union_info_get_n_fields,
	       gi.union_info_get_field,
	       function(c, n, i)
		  if check_type(gi.field_info_get_type(i)) then c[n] = i end
	       end },
	 }, struct_mt)
      return value, '_unions'
   end

-- Removes accessor methods for properties.  Properties should be accessed as
-- properties, not by function calls, and this removes some clutter and memory
-- overhead.
local function remove_property_accessors(compound)
   if compound._methods then
      for propname in pairs(compound._properties or {}) do
	 compound._methods['get_' .. propname] = nil
	 compound._methods['set_' .. propname] = nil
	 compound._methods[propname] = nil
      end
   end
end

typeloader[gi.InfoType.INTERFACE] =
   function(namespace, info)
      -- Load all components of the interface.
      local value = {}
      load_compound(
	 value, info,
	 {
	    _properties = { gi.interface_info_get_n_properties,
			    gi.interface_info_get_property,
			    function(c, n, i)
			       if check_type(gi.property_info_get_type(i)) then
				  c[string.gsub(n, '%-', '_')] = i
			       end
			    end },
	    _methods = {
	       gi.interface_info_get_n_methods,
	       gi.interface_info_get_method,
	       function(c, n, i)
		  local flags = gi.function_info_get_flags(i)
		  if bit.band(
		     flags, bit.bor(gi.FunctionInfoFlags.IS_GETTER,
				    gi.FunctionInfoFlags.IS_SETTER)) == 0 then
		     if check_type(i) then
			c[n] = core.get(i)
		     end
		  end
	       end },
	    _signals = { gi.interface_info_get_n_signals,
			 gi.interface_info_get_signal,
			 function(c, n, i)
			    if check_type(i) then
			       c['on_' .. string.gsub(n, '%-', '_')] = i
			    end
			 end },
	    _constants = { gi.interface_info_get_n_constants,
			   gi.interface_info_get_constant,
			   function(c, n, i)
			      if check_type(gi.constant_info_get_type(i)) then
				 c[n] = core.get(i)
			      end
			   end },
	    _inherits = { gi.interface_info_get_n_prerequisites,
			  gi.interface_info_get_prerequisite,
			  function(c, n, i)
			     local ns = gi.BaseInfo.get_namespace(i)
			     c[ns .. '.' .. n] = repo[ns][n]
			  end },
	 }, interface_mt)
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
			    if check_type(gi.property_info_get_type(i)) then
			       c[string.gsub(n, '%-', '_')] = i
			    end
			 end },
	 _methods = { gi.object_info_get_n_methods,
		      gi.object_info_get_method,
		      function(c, n, i)
			 if check_type(i) then
			    c[n] = core.get(i)
			 end
		      end },
	 _signals = { gi.object_info_get_n_signals,
		      gi.object_info_get_signal,
		      function(c, n, i)
			 if check_type(i) then
			    c['on_' .. string.gsub(n, '%-', '_')] = i
			 end
		      end },
	 _constants = { gi.object_info_get_n_constants,
			gi.object_info_get_constant,
			function(c, n, i)
			   if check_type(gi.constant_info_get_type(i)) then
			      c[n] = core.get(i)
			   end
			end },
	 _inherits = { gi.object_info_get_n_interfaces,
		       gi.object_info_get_interface,
		       function(c, n, i)
			  local ns = gi.BaseInfo.get_namespace(i)
			  c[ns .. '.' .. n] = repo[ns][n]
		       end },
      }, class_mt)

   -- Add parent (if any) into _inherits table.
   local parent = gi.object_info_get_parent(info)
   if parent then
      local ns, name = gi.BaseInfo.get_namespace(parent),
      gi.BaseInfo.get_name(parent)
      if ns ~= namespace[0].name or name ~= gi.BaseInfo.get_name(info) then
	 into._inherits = into._inherits or {}
	 into._inherits[ns .. '.' .. name] = repo[ns][name]
      end
   end
   remove_property_accessors(into)
end

typeloader[gi.InfoType.OBJECT] =
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
   local info = ir:find_by_name(namespace[0].name, symbol)

   -- Store the symbol into the in-load table, because we have to
   -- avoid infinte recursion which might happen during type
   -- validation if there are cycles in type definitions (e.g. struct
   -- defining method having as an argument pointer to the struct).
   local fullname = namespace[0].name .. '.' .. symbol
   in_load[fullname] = info

   -- Decide according to symbol type what to do.
   if info and not gi.BaseInfo.is_deprecated(info) then
      local infotype = gi.base_info_get_type(info)
      local loader = typeloader[infotype]
      if loader then
	 local category
	 value, category = loader(namespace, info)

	 if value then
	    -- Cache the symbol in specified category in the namespace.
	    local cat = rawget(namespace, category) or {}
	    namespace[category] = cat
	    cat[symbol] = value
	 else
	    loginfo('symbol %s refused', fullname)
	 end
      end
   end

   in_load[fullname] = nil
   return value
end

-- Loads namespace, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
local function load_namespace(into, name)
   -- If package does not exist yet, create and store it into packages.
   if not into then
      into = {}
      repo[name] = into
   end

   -- Create _meta table containing auxiliary information
   -- and data for the namespace.  This table also serves as metatable for the
   -- namespace, providing __index method for retrieveing namespace content.
   into[0] = { name = name, dependencies = {}, __index = get_symbol }
   setmetatable(into, into[0])

   -- Load the typelibrary for the namespace.
   if not ir:require(name, nil, 0) then return nil end
   into[0].version = ir:get_version(name)

   -- Load all namespace dependencies.
   for _, name in pairs(ir:get_dependencies(name) or {}) do
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
	 for i = 0, ir:get_n_infos(name) - 1 do
	    local info = ir:get_info(name, i)
	    get_symbol(into, gi.BaseInfo.get_name(info))
	 end
      end
   return into
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(repo, name)
				  return load_namespace(nil, name)
			       end })

-- Convert our poor-man's GIRepository namespace into full-featured one.
loginfo 'upgrading repo.GIRepository to full-featured namespace'
gi._enums.InfoType = nil
gi._enums.TypeTag = nil
gi._enums.ArrayType = nil
gi._enums.FunctionInfoFlags = nil
load_namespace(gi, 'GIRepository')
load_class(gi, gi._classes.Repository,
	   ir:find_by_name(gi[0].name, 'Repository'))
load_struct(gi, gi._structs.Typelib,
	    ir:find_by_name(gi[0].name, 'Typelib'))
load_struct(gi, gi._structs.BaseInfo,
	    ir:find_by_name(gi[0].name, 'BaseInfo'))

-- GObject.Object massaging
do
   local obj = repo.GObject.Object

   -- No methods are needed (yet).
   obj._methods = nil

   -- Install 'notify' signal.
   local obj_struct = ir:find_by_name('GObject', 'ObjectClass')
   for i = 0, gi.struct_info_get_n_fields(obj_struct) - 1 do
      local field = gi.struct_info_get_field(obj_struct, i)
      if field:get_name() == 'notify' then
	 obj._signals = {
	    on_notify = gi.type_info_get_interface(
	       gi.field_info_get_type(field))
	 }
	 break
      end
   end
end

-- Install new loader which will load lgi packages on-demand using 'repo'
-- table.
loginfo 'installing custom Lua package loader'
package.loaders[#package.loaders + 1] =
   function(name)
      local prefix, name = string.match(name, '^(%w+)%.(%w+)$')
      if prefix == 'lgi' then
	 return function() return repo[name] end
      end
   end

-- Access to module proxies the whole repo, for convenience.
setmetatable(_M, { __index = function(_, name) return repo[name] end })
