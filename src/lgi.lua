--[[--------------------------------------------------------------------------

    Lgi bootstrapper.

    Copyright (c) 2010 Pavel Holejsovsky
    Licensed under the MIT license:
    http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local assert, setmetatable, getmetatable, type, pairs, string, rawget,
   table, require, tostring, error, pcall, ipairs, unpack =
      assert, setmetatable, getmetatable, type, pairs, string, rawget,
      table, require, tostring, error, pcall, ipairs, unpack
local package, math = package, math
local bit = require 'bit'

-- Require core lgi utilities, used during bootstrap.
local core = require 'lgi._core'

module 'lgi'

-- Prepare logging support.  'log' is module-exported table, containing all
-- functionality related to logging wrapped around GLib g_log facility.
log = { ERROR = 'assert', DEBUG = 'silent' }
core.setlogger(
   function(domain, level, message)
      -- Create domain table in the log table if it does not exist yet.
      if not log[domain] then log[domain] = {} end

      -- Check whether message should generate assert (i.e. Lua exception).
      local setting = log[domain][level] or log[level]
      if setting == 'assert' then error() end
      if setting == 'silent' then return true end

      -- Get handler for the domain and invoke it.
      local handler = log[domain].handler or log.handler
      return handler and handler(domain, level, message)
   end)

-- Main logging facility.
function log.log(domain, level, format, ...)
   local ok, msg = pcall(string.format, format, ...)
   if not ok then msg = ("BAD FMT: `%s', `%s'"):format(format, msg) end
   core.log(domain, level, msg)
end

-- Creates table containing methods 'message', 'warning', 'critical', 'error',
-- 'debug' methods which log to specified domain.
function log.domain(name)
   local domain = log[name] or {}
   for _, level in ipairs { 'message', 'warning', 'critical',
			    'error', 'debug' } do
      if not domain[level] then
	 domain[level] = function(format, ...)
			    log.log(name, level:upper(), format, ...)
			 end
      end
   end
   log[name] = domain
   return domain
end

-- For the rest of bootstrap, prepare logging to Lgi domain.
local log = log.domain('Lgi')

log.message('Lgi: Lua to GObject-Introspection binding v0.1')

local ir

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

-- Loads symbol from specified compound (object, struct or interface).
-- Recursively looks up inherited elements.
local function find_in_compound(compound, symbol, categories)
   -- Tries to get symbol from specified category.
   local function from_category(compound, category, symbol)
      local cat = rawget(compound, category)
      return cat and cat[symbol]
   end

   -- Check fields of this compound.
   local prefix, name = string.match(symbol, '^(_.-)_(.*)$')
   if prefix and name then
      -- Look in specified category.
      local val = from_category(compound, prefix, name)
      if val then return val end
   else
      -- Check all available categories.
      for i = 1, #categories do
	 local val = from_category(compound, categories[i], symbol)
	 if val then return val end
      end
   end

   -- Check all inherited compounds.
   for _, inherited in pairs(rawget(compound, '_inherits') or {}) do
      local val = find_in_compound(inherited, symbol, categories)
      if val then return val end
   end
end

-- Fully resolves the whole compound, i.e. load all symbols normally loaded
-- on-demand at once.
local function resolve_compound(compound_meta)
   local ns, name = string.match(compound_meta.name, '(.+)%.(.+)')
   for _, category in pairs(repo[ns][name]) do
      if type(category) == 'table' then local _ = category[0] end
   end
end

-- Metatable for namespaces.
local namespace_mt = {}
function namespace_mt.__index(namespace, symbol)
   return find_in_compound(namespace, symbol,
			   { '_classes', '_interfaces', '_structs', '_unions',
			     '_enums', '_functions', '_constants', })
end

-- Metatable for structs, allowing to 'call' structure, which is
-- translating to creating new structure instance (i.e. constructor).
local struct_mt = {}
function struct_mt.__index(struct, symbol)
   return find_in_compound(struct, symbol, { '_methods', '_fields' })
end

function struct_mt.__call(type, fields)
   -- Create the structure instance.
   local info
   if type[0].type then
      -- Lookup info by gtype.
      info = assert(ir:find_by_gtype(type[0].gtype))
   else
      -- GType is not available, so lookup info by name.
      info = assert(ir:find_by_name(type[0].name:match('^(.-)%.(.+)$')))
   end
   local struct = core.construct(info)

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

-- Metatable for enum type tables.
local enum_mt = {}
function enum_mt.__index(enum, value)
   for name, val in pairs(enum) do
      if val == value then return name end
   end
end

-- Namespace table for GIRepository, populated with basic methods
-- manually.  Later it will be converted to full-featured repo namespace.
local gi = setmetatable({ [0] = { name = 'GIRepository' } }, namespace_mt)
core.repo.GIRepository = gi

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
					 FIELD = 16,
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
     }, struct_mt),
}

-- Loads given set of symbols into table.
local function get_symbols(into, symbols, container)
   for _, symbol in pairs(symbols) do
      into[symbol] = core.construct(core.find(symbol, container))
   end
end

-- Metatable for interfaces, implementing casting on __call.
local interface_mt = {}
function interface_mt.__index(iface, symbol)
   return find_in_compound(iface, symbol, { '_properties', '_methods',
					    '_signals', '_constants' })
end
function interface_mt.__call(iface, obj)
   -- Cast operator, 'param' is source object which should be cast.
   local res = iface and core.cast(obj, iface[0].gtype)
   if not res then
      error(string.format("`%s' cannot be cast to `%s'", tostring(obj),
			  iface[0].name));
   end
   return res
end

-- Metatable for classes, implementing object construction or casting
-- on __call.
local class_mt = {}
function class_mt.__index(class, symbol)
   return find_in_compound(class, symbol, {
			      '_properties', '_methods',
			      '_signals', '_constants', '_fields' })
end
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
      local others = {}

      -- Get BaseInfo from gtype.
      local info = assert(ir:find_by_gtype(class[0].gtype))

      -- Process 'param' table, create constructor property table and signals
      -- table.
      for name, value in pairs(param or {}) do
	 local paramtype = class[name]
	 if (type(paramtype) == 'userdata' and
	  gi.base_info_get_type(paramtype) == gi.InfoType.PROPERTY) then
	    params[paramtype] = value
	 else
	    others[name] = value
	 end
      end

      -- Create the object.
      obj = core.construct(info, params)

      -- Attach signals previously filtered out from creation.
      for name, func in pairs(others) do obj[name] = func end
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
      'signal_info_get_flags',
      'arg_info_get_type',
      'constant_info_get_type',
      'property_info_get_type',
      'field_info_get_type',
      })

-- Remember default repository.
ir = gi.Repository.get_default()

log.debug 'repo.GIRepository pre-populated'

-- Weak table containing symbols which currently being loaded. It is used
-- mainly to avoid assorted kinds of infinite recursion which might happen
-- during symbol dependencies loading.
local in_load = setmetatable({}, { __mode = 'v' })

-- Table containing loaders for various GI types, indexed by
-- gi.InfoType constants.
local typeloader = {}

-- Loads the type and all dependent subtypes.  Returns nil if it cannot be
-- loaded or the type itself if it is ok.
local function check_type(info)
   if not info then return nil end
   local type = gi.base_info_get_type(info)
   if type == gi.InfoType.TYPE then
      -- Check the embedded typeinfo.
      local tag = gi.type_info_get_tag(info)
      if tag < gi.TypeTag.ARRAY then return info
      elseif tag == gi.TypeTag.ARRAY then
	 return check_type(gi.type_info_get_param_type(info, 0)) and info
      elseif tag == gi.TypeTag.INTERFACE then
	 return check_type(gi.type_info_get_interface(info)) and info
      elseif tag == gi.TypeTag.GLIST or tag == gi.TypeTag.GSLIST then
	 return check_type(gi.type_info_get_param_type(info, 0)) and info
      elseif tag == gi.TypeTag.GHASH then
	 return (check_type(gi.type_info_get_param_type(info, 0)) and
	      check_type(gi.type_info_get_param_type(info, 1))) and info
      else
	 log.warning('unknown typetag %s(%d)', tostring(gi.TypeTag[tag]), tag)
	 return nil
      end
   elseif (type == gi.InfoType.FUNCTION or type == gi.InfoType.CALLBACK or
	   type == gi.InfoType.SIGNAL or type == gi.InfoType.VFUNC) then
      -- Check all callable arguments and return value.
      if not check_type(gi.callable_info_get_return_type(info)) then
	 return nil
      end
      for i = 0, gi.callable_info_get_n_args(info) - 1 do
	 local ai = gi.callable_info_get_arg(info, i)
	 if not check_type(gi.arg_info_get_type(ai)) then
	    return nil
	 end
      end
      return info
   elseif type == gi.InfoType.CONSTANT then
      return check_type(gi.constant_info_get_type(info)) and info
   elseif type == gi.InfoType.PROPERTY then
      return check_type(gi.property_info_get_type(info)) and info
   elseif type == gi.InfoType.FIELD then
      return check_type(gi.field_info_get_type(info)) and info
   elseif (type == gi.InfoType.STRUCT or type == gi.InfoType.ENUM or
	   type == gi.InfoType.FLAGS or type == gi.InfoType.OBJECT or
	   type == gi.InfoType.INTERFACE or type == gi.InfoType.UNION) then
      -- Check, whether we can reach the symbol in the repo.
      local ns, n = info:get_namespace(), info:get_name()
      return (in_load[ns .. '.' .. n] or repo[ns][n]) and info
   else
      local name = {}
      while info do
	 if gi.base_info_get_type(info) ~= gi.InfoType.TYPE then
	    table.insert(name, 1, info:get_name())
	 end
	 info = info:get_container()
      end
      log.warning("unknown type %s of %s", gi.InfoType[type],
		  table.concat(name, '.'))
      return nil
   end
end

typeloader[gi.InfoType.FUNCTION] =
   function(namespace, info)
      return check_type(info) and core.construct(info), '_functions'
   end

typeloader[gi.InfoType.CONSTANT] =
   function(namespace, info)
      return check_type(info) and core.construct(info), '_constants'
   end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   for i = 0, gi.enum_info_get_n_values(info) - 1 do
      local mi = gi.enum_info_get_value(info, i)
      local name = string.upper(mi:get_name())
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

-- Gets table for category of compound (i.e. _fields of struct or _properties
-- for class etc).  Installs metatable which performs on-demand lookup of
-- symbols.
local function get_category(info, count, get_item, xform_value, xform_name,
			    xform_name_reverse, original_table)
   assert(not xform_name or xform_name_reverse)

   -- Early shortcircuit; no elements, no table needed at all.
   if count == 0 then return original_table end

   -- Index contains array of indices which were still not retrieved
   -- by get_info method, and table part contains name->index mapping.
   local index = {}
   for i = 1, count do index[i] = i - 1 end
   local cached_names = 0
   return setmetatable(
      original_table or {}, { __index =
	    function(category, req_name)
	       -- Querying index 0 has special semantics; makes the
	       -- whole table fully loaded.
	       if req_name == 0 then
		  local ei, en, val, ok

		  -- Load al values from unknown indices.
		  while #index > 0 do
		     ok, ei = pcall(get_item, info, table.remove(index))
		     if not ok then ei = nil else ei = check_type(ei) end
		     val = not xform_value and ei or xform_value(ei)
		     if val then
			en = ei:get_name()
			if xform_name_reverse then
			   en = xform_name_reverse(en, ei)
			end
			if en then category[en] = val end
		     end
		  end

		  -- Load all known indices.
		  for en, idx in pairs(index) do
		     ok, val = pcall(get_item, info, idx)
		     if not ok then val = nil else val = check_type(val) end
		     val = not xform_value and val or xform_value(val)
		     category[en] = val
		  end

		  -- Metatable is no longer needed, disconnect it.
		  setmetatable(category, nil)
		  return nil
	       end

	       -- Transform name by transform function.
	       local name = not xform_name and req_name or xform_name(req_name)
	       if not name then return end

	       -- Check, whether we already know its index.
	       local idx, val = index[name]
	       if idx then
		  -- We know at least the index, so get info directly.
		  val = get_item(info, idx)
		  index[name] = nil
		  cached_names = cached_names - 1
	       else
		  -- Not yet, go through unknown indices and try to
		  -- find the name.
		  while #index > 0 do
		     idx = table.remove(index)
		     val = get_item(info, idx)
		     local en = val:get_name()
		     if en == name then break end
		     val = nil
		     index[en] = idx
		     cached_names = cached_names + 1
		  end
		  if not val then return nil end
	       end

	       -- If there is nothing in the index, we can disconnect
	       -- metatable, because everything is already loaded.
	       if #index == 0 and cached_names == 0 then
		  setmetatable(category, nil)
	       end

	       -- Transform found value and store it into the category table.
	       if not check_type(val) then return nil end
	       if xform_value then val = xform_value(val) end
	       if not val then return nil end
	       category[req_name] = val
	       return val
	    end
      })
end

-- Loads all fields, consts, properties, methods and interfaces of given
-- object.
local function load_compound(compound, info, mt)
   -- Fill in meta of the compound.
   compound[0] = compound[0] or {}
   compound[0].gtype = gi.registered_type_info_get_g_type(info)
   if compound[0].gtype == 4 then
      -- Non-boxed struct, it doesn't have any gtype.
      compound[0].gtype = nil
   end
   compound[0].name = (info:get_namespace() .. '.'  .. info:get_name())
   compound[0].resolve = resolve_compound
   setmetatable(compound, mt)
end

local function load_element_field(fi)
   return function(obj, _, newval) return core.elementof(obj, fi, newval) end
end

local function load_element_property(pi)
   return function(obj, _, newval) return core.elementof(obj, pi, newval) end
end

local function load_signal_name(name)
   name = string.match(name, '^on_(.+)$')
   return name and string.gsub(name, '_', '%-')
end

local function load_signal_name_reverse(name)
   return 'on_' .. string.gsub(name, '%-', '_')
end

local function load_element_signal(info)
   return
   function(obj, _, newval)
      if newval then
	 -- Assignment means 'connect signal without detail'.
	 core.connect(obj, info:get_name(), info, newval)
      else
	 -- Reading yields table with signal operations.
	 local pad = {
	    connect = function(_, target, detail, after)
			 return core.connect(obj, info:get_name(), info, target,
					     detail, after)
		      end,
	 }

	 -- If signal supports details, add metatable implementing __newindex
	 -- for connecting in the 'on_signal['detail'] = handler' form.
	 if (gi.base_info_get_type(info) == gi.InfoType.SIGNAL and
	  bit.band(gi.signal_info_get_flags(info), 16) ~= 0) then
	    setmetatable(pad, {
			    __newindex = function(obj, detail, target)
					    core.connect(obj, info:get_name(),
							 info, newval, detail)
					 end
			 })
	 end

	 -- Return created signal pad.
	 return pad
      end
   end
end

-- Loads structure information into table representing the structure
local function load_struct(namespace, struct, info)
   -- Avoid exposing internal structs created for object implementations.
   if not gi.struct_info_is_gtype_struct(info) then
      load_compound(struct, info, struct_mt)
      struct._methods = get_category(
	 info, gi.struct_info_get_n_methods(info), gi.struct_info_get_method,
	 core.construct, nil, nil, rawget(struct, '_methods'))
      struct._fields = get_category(
	 info, gi.struct_info_get_n_fields(info), gi.struct_info_get_field,
	 load_element_field)
   end
end

typeloader[gi.InfoType.STRUCT] =
   function(namespace, info)
      local struct = {}
      load_struct(namespace, struct, info)
      return struct, '_structs'
   end

typeloader[gi.InfoType.UNION] =
   function(namespace, info)
      local union = {}
      load_compound(union, info, struct_mt)
      union._methods = get_category(
	 info, gi.union_info_get_n_methods(info), gi.union_info_get_method,
	 core.construct)
      union._fields = get_category(
	 info, gi.union_info_get_n_fields(info), gi.union_info_get_field,
	 load_element_field)
      return union, '_unions'
   end

typeloader[gi.InfoType.INTERFACE] =
   function(namespace, info)
      -- Load all components of the interface.
      local interface = {}
      load_compound(interface, info, interface_mt)
      interface._properties = get_category(
	 info, gi.interface_info_get_n_properties(info),
	 gi.interface_info_get_property, load_element_property,
	 function(name) return string.gsub(name, '_', '%-') end,
	 function(name) return string.gsub(name, '%-', '_') end)
      interface._methods = get_category(
	 info, gi.interface_info_get_n_methods(info),
	 gi.interface_info_get_method,
	 function(ii)
	    local flags = gi.function_info_get_flags(ii)
	    if bit.band(flags, gi.FunctionInfoFlags.IS_GETTER
			+ gi.FunctionInfoFlags.IS_SETTER) == 0 then
	       return core.construct(ii)
	    end
	 end)
      interface._signals = get_category(
	 info, gi.interface_info_get_n_signals(info),
	 gi.interface_info_get_signal, load_element_signal,
	 load_signal_name, load_signal_name_reverse)
      interface._constants = get_category(
	 info, gi.interface_info_get_n_constants(info),
	 gi.interface_info_get_constant, core.construct)
      interface._inherits = get_category(
	 info, gi.interface_info_get_n_prerequisites(info),
	 gi.interface_info_get_prerequisite,
	 function(ii)
	    local ns, n = ii:get_namespace(), ii:get_name()
	    -- Avoid circular dependencies; in case that prerequisity
	    -- is to some type which is currently being loaded,
	    -- disregard it.
	    if not in_load[ns .. '.' .. n] then return repo[ns][n] end
	 end,
	 nil,
	 function(info_name, ii)
	    return ii:get_namespace() .. '.' .. info_name
	 end)
      -- Immediatelly fully resolve the table.
      local _ = rawget(interface, '_inherits') and interface._inherits[0]
      return interface, '_interfaces'
   end

-- Loads structure information into table representing the structure
local function load_class(namespace, class, info)
   -- Load components of the object.
   load_compound(class, info, class_mt)
   class._properties = get_category(
      info, gi.object_info_get_n_properties(info), gi.object_info_get_property,
      load_element_property,
      function(n) return (string.gsub(n, '_', '%-')) end,
      function(n) return (string.gsub(n, '%-', '_')) end)
   class._methods = get_category(
      info, gi.object_info_get_n_methods(info), gi.object_info_get_method,
      function(mi)
	 local flags = gi.function_info_get_flags(mi)
	 if bit.band( flags, (gi.FunctionInfoFlags.IS_GETTER
			      + gi.FunctionInfoFlags.IS_SETTER)) == 0 then
	    return core.construct(mi)
	 end
      end, nil, nil, rawget(class, '_methods'))
   class._signals = get_category(
      info, gi.object_info_get_n_signals(info), gi.object_info_get_signal,
      load_element_signal, load_signal_name, load_signal_name_reverse)
   class._constants = get_category(
      info, gi.object_info_get_n_constants(info), gi.object_info_get_constant,
      core.construct)
   class._inherits = get_category(
      info, gi.object_info_get_n_interfaces(info), gi.object_info_get_interface,
      function(ii) return repo[ii:get_namespace(ii)][ii:get_name()] end,
      nil,
      function(n, ii) return ii:get_namespace() .. '.' .. n end)
   local _ = rawget(class, '_inherits') and class._inherits[0]

   -- Add parent (if any) into _inherits table.
   local parent = gi.object_info_get_parent(info)
   if parent then
      local ns, name = parent:get_namespace(), parent:get_name()
      if ns ~= namespace[0].name or name ~= info:get_name() then
	 class._inherits = rawget(class, '_inherits') or {}
	 class._inherits[ns .. '.' .. name] = repo[ns][name]
      end
   end
end

typeloader[gi.InfoType.OBJECT] =
   function(namespace, info)
      local class = {}
      load_class(namespace, class, info)
      return class, '_classes'
   end

-- Gets symbol of the specified namespace, if not present yet, tries to load it
-- on-demand.
local namespace_lookup = namespace_mt.__index
function namespace_mt.__index(namespace, symbol)
   -- Check, whether symbol is already loaded.
   local value = namespace_lookup(namespace, symbol)
   if value then return value end

   -- Lookup baseinfo of requested symbol in the repo.
   local info = ir:find_by_name(namespace[0].name, symbol)

   -- Store the symbol into the in-load table, because we have to
   -- avoid infinte recursion which might happen during symbol loading (mainly
   -- when prerequisity of the interface is the class which implements said
   -- interface).
   local fullname = namespace[0].name .. '.' .. symbol
   in_load[fullname] = info

   -- Decide according to symbol type what to do.
   if info and not info:is_deprecated() then
      local infotype = gi.base_info_get_type(info)
      local loader = typeloader[infotype]
      if loader then
	 local category
	 value, category = loader(namespace, info)

	 -- Cache the symbol in specified category in the namespace.
	 local cat = rawget(namespace, category) or {}
	 namespace[category] = cat
	 cat[symbol] = value
      end
   end

   in_load[fullname] = nil
   return value
end

-- Resolves everything in the namespace by iterating through it.
local function resolve_namespace(ns_meta)
   -- Iterate through all items in the namespace and dereference them,
   -- which causes them to be loaded in and cached inside the namespace
   -- table.
   local name = ns_meta.name
   local ns = repo[name]
   for i = 0, ir:get_n_infos(name) - 1 do
      pcall(function() local _ = ns[ir:get_info(name, i):get_name()] end)
   end
end

-- Loads namespace, optionally with specified version and returns table which
-- represents it (usable as package table for Lua package loader).
local function load_namespace(into, name)
   -- If package does not exist yet, create and store it into packages.
   assert(name ~= 'val')
   if not into then
      into = {}
      repo[name] = into
   end

   -- Create _meta table containing auxiliary information
   -- and data for the namespace.
   into[0] = { name = name, dependencies = {} }
   setmetatable(into, namespace_mt)

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
   into[0].resolve = resolve_namespace
   return into
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(repo, name)
				  return load_namespace(nil, name)
			       end })

-- Convert our poor-man's GIRepository namespace into full-featured one.
log.debug('upgrading repo.GIRepository to full-featured namespace')
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

-- GObject modifications.
do
   local obj = repo.GObject.Object

   -- No methods are needed (yet).
   obj._methods = nil

   -- Install 'on_notify' signal.  There are a few gotchas causing that the
   -- signal is handled separately from common signal handling above:
   -- 1) There is no declaration of this signal in the typelib.
   -- 2) Real notify works with glib-style property names as details.  We use
   --    real type properties (e.g. Gtk.Window.has_focus) instead.
   -- 3) notify handler gets pspec, but we pass Lgi-mangled property name
   --    instead.

   -- Real on_notify handler, converts its parameter from pspec to Lgi-style
   -- property name.
   local function get_notifier(target)
      return function(obj, pspec)
		return target(obj, (string.gsub(pspec.name, '%-', '_')))
	     end
   end

   -- Implementation of on_notify worker function.
   local function on_notify(obj, info, newval)
      if newval then
	 -- Assignment means 'connect signal for all properties'.
	 core.connect(obj, info:get_name(), info, get_notifier(newval))
      else
	 -- Reading yields table with signal operations.
	 local pad = {
	    connect = function(_, target, property)
			 return core.connect(obj, info:get_name(), info,
					     get_notifier(target),
					     property:get_name())
		      end,
	 }

	 -- Add metatable allowing connection on specified detail.  Detail is
	 -- always specified as a property for this signal.
	 setmetatable(pad, {
			 __newindex = function(_, property, target)
					 core.connect(obj, info:get_name(),
						      info,
						      get_notifier(target),
						      property:get_name())
				      end
			 })

	 -- Return created signal pad.
	 return pad
      end
   end

   -- Install 'notify' signal.  Unfortunately typelib does not contain its
   -- declaration, so we borrow it from callback GObject.ObjectClass.notify.
   local obj_struct = ir:find_by_name('GObject', 'ObjectClass')
   for i = 0, gi.struct_info_get_n_fields(obj_struct) - 1 do
      local field = gi.struct_info_get_field(obj_struct, i)
      if field:get_name() == 'notify' then
	 obj._signals = {
	    on_notify = function(obj, _, newval)
			   return on_notify(obj, gi.type_info_get_interface(
					       gi.field_info_get_type(field)),
					    newval)
			end,
	 }
	 break
      end
   end

   -- Closure modifications.  Closure does not need any methods nor
   -- fields, but it must have constructor creating it from any kind
   -- of Lua callable.
   local closure = repo.GObject.Closure
   local closure_info = ir:find_by_name('GObject', 'Closure')
   closure._methods = nil
   closure._fields = nil
   setmetatable(closure, { __index = getmetatable(closure).__index,
			   __tostring = getmetatable(closure).__tostring,
			   __call = function(_, arg)
				       return core.construct(closure_info, arg)
				    end })

   -- Value is constructible from any kind of source Lua
   -- value, and the type of the value can be hinted by type name.
   local value = repo.GObject.Value
   local value_info = ir:find_by_name('GObject', 'Value')

   -- Tries to deduce the gtype according to Lua value.
   local function gettype(source)
      if source == nil then
	 return 'void'
      elseif type(source) == 'boolean' then
	 return 'gboolean'
      elseif type(source) == 'number' then
	 -- If the number fits in integer, use it, otherwise use double.
	 local _, fract = math.modf(source)
	 local maxint32 = -bit.lshift(1, 31)
	 return ((fract == 0 and source >= -maxint32 and source < maxint32)
	      and 'gint' or 'gdouble')
      elseif type(source) == 'string' then
	 return 'gchararray'
      elseif type(source) == 'function' then
	 -- Generate closure for any kind of function.
	 return closure[0].gtype
      elseif type(source) == 'userdata' then
	 -- Examine type of userdata.
	 local meta = getmetatable(source)
	 if meta and meta.__call then
	    -- It seems that it is possible to call on this, so generate
	    -- closure.
	    return closure[0].gtype
	 elseif meta == getmetatable(value_info) then
	    -- Some kind of compound, get its real gtype from core.
	    return core.gtype(source)
	 end
      elseif type(source) == 'table' then
	 -- Check, whether we can call it.
	 local meta = getmetatable(source)
	 if meta and meta.__call then
	    return closure[0].gtype
	 end
      end

      -- No idea to what type should this be mapped.
      error(string.format("unclear type for GValue from argument `%s'",
			  tostring(source)))
   end

   -- Implement constructor, taking any source value and optionally type of the
   -- source.
   local value_mt = { __index = struct_mt.__index }
   function value_mt:__call(source, stype)
      stype = stype or gettype(source)
      if type(stype) == 'string' then
	 stype = repo.GObject.type_from_name(stype)
      end
      return core.construct(value_info, stype, source)
   end
   setmetatable(value, value_mt)
   value._methods = nil
   value._fields = { _g_type = value._fields.g_type }
   function value._fields.type(val, _, newval)
      assert(not newval, "GObject.Value: `type' not writable")
      return repo.GObject.type_name(val._fields__g_type) or ''
   end
   function value._fields.value(val, _, newval)
      assert(not newval, "GObject.Value: `value' not writable")
      return core.construct(val)
   end
end

-- Install new loader which will load lgi packages on-demand using 'repo'
-- table.
log.debug('installing custom Lua package loader')
package.loaders[#package.loaders + 1] =
   function(name)
      local prefix, name = string.match(name, '^(%w+)%.(%w+)$')
      if prefix == 'lgi' then
	 return function() return repo[name] end
      end
   end

-- Access to module proxies the whole repo, for convenience.
setmetatable(_M, { __index = function(_, name) return repo[name] end })
