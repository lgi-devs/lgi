------------------------------------------------------------------------------
--
--  LGI Lua-side core.
--
--  Copyright (c) 2010 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local assert, setmetatable, getmetatable, type, pairs, string, rawget,
table, require, tostring, error, pcall, ipairs, unpack,
next, select =
   assert, setmetatable, getmetatable, type, pairs, string, rawget,
   table, require, tostring, error, pcall, ipairs, unpack or table.unpack,
   next, select
local package, math = package, math

-- Require core lgi utilities, used during bootstrap.
local core = require 'lgi._core'

-- Initialize GI wrapper from the core.
local gi = core.gi
assert(gi.require ('GLib', '2.0'))
assert(gi.require ('GObject', '2.0'))

-- Create lgi table, containing the module.
local lgi = {}

-- Prepare logging support.  'log' is module-exported table, containing all
-- functionality related to logging wrapped around GLib g_log facility.
local logtable = { ERROR = 'assert', DEBUG = 'silent' }
lgi.log = logtable
core.setlogger(
   function(domain, level, message)
      -- Create domain table in the log table if it does not exist yet.
      if not logtable[domain] then logtable[domain] = {} end

      -- Check whether message should generate assert (i.e. Lua exception).
      local setting = logtable[domain][level] or logtable[level]
      if setting == 'assert' then error() end
      if setting == 'silent' then return true end

      -- Get handler for the domain and invoke it.
      local handler = logtable[domain].handler or logtable.handler
      return handler and handler(domain, level, message)
   end)

-- Main logging facility.
function logtable.log(domain, level, format, ...)
   local ok, msg = pcall(string.format, format, ...)
   if not ok then msg = ("BAD FMT: `%s', `%s'"):format(format, msg) end
   core.log(domain, level, msg)
end

-- Creates table containing methods 'message', 'warning', 'critical', 'error',
-- 'debug' methods which log to specified domain.
function logtable.domain(name)
   local domain = logtable[name] or {}
   for _, level in ipairs { 'message', 'warning', 'critical',
			    'error', 'debug' } do
      if not domain[level] then
	 domain[level] = function(format, ...)
			    logtable.log(name, level:upper(), format, ...)
			 end
      end
   end
   logtable[name] = domain
   return domain
end

-- For the rest of bootstrap, prepare logging to Lgi domain.
local log = logtable.domain('Lgi')

log.message('Lgi: Lua to GObject-Introspection binding v0.1')

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

local function default_element_impl(compound, _, symbol)
   return compound[symbol]
end

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
   elseif string.sub(symbol, 1, 1) ~= '_' then
      -- Check all available categories.
      for i = 1, #categories do
	 local val = from_category(compound, categories[i], symbol)
	 if val then return val end
      end
   end

   -- Check parent and all implemented compounds.
   local val = (rawget(compound, '_parent') or {})[symbol]
   if val then return val end
   for _, implemented in pairs(rawget(compound, '_implements') or {}) do
      val = implemented[symbol]
      if val then return val end
   end

   -- Metatable might implement some specific methods.
   local meta = getmetatable(compound)
   return meta and meta[symbol]
end

-- Fully resolves the whole compound, i.e. load all symbols normally loaded
-- on-demand at once.
local function resolve_compound(compound)
   local ns, name = string.match(compound._name, '(.+)%.(.+)')
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
function struct_mt:__index(symbol)
   return find_in_compound(self, symbol, { '_methods', '_fields' })
end

function struct_mt:__call(fields)
   -- Create the structure instance.
   local info
   if self._gtype then
      -- Lookup info by gtype.
      info = assert(gi[self._gtype])
   else
      -- GType is not available, so lookup info by name.
      local ns, name = self._name:match('^(.-)%.(.+)$')
      info = assert(gi[ns][name])
   end
   local struct = core.record.new(info)

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
      if type(flag) == 'number' and value % (2 * flag) >= flag then
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

-- Metatable for interfaces.
local interface_mt = {}
function interface_mt.__index(iface, symbol)
   return find_in_compound(iface, symbol, { '_properties', '_methods',
					    '_signals', '_constants' })
end

-- Metatable for classes, implementing object construction on __call.
local class_mt = {}
function class_mt.__index(class, symbol)
   return find_in_compound(class, symbol, {
			      '_properties', '_methods',
			      '_signals', '_constants', '_fields' })
end

-- Object constructor, 'param' contains table with properties/signals
-- to initialize.
function class_mt.__call(class, param)
   local params = {}
   local others = {}

   -- Process 'param' table, create constructor property table and signals
   -- table.
   for name, value in pairs(param or {}) do
      local paramtype = class[name]
      if type(paramtype) == 'function' then paramtype = paramtype() end
      if type(paramtype) == 'userdata' and paramtype.is_property then
	 params[paramtype] = value
      else
	 others[name] = value
      end
   end

   -- Create the object.
   local obj = core.object.new(class._gtype, params)

   -- Attach signals previously filtered out from creation.
   for name, func in pairs(others) do obj[name] = func end
   return obj
end

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
   local type = info.type
   if type == 'type' then
      -- Check the embedded typeinfo.
      local tag = info.tag
      if info.is_basic then
	 return info
      elseif tag == 'array' then
	 return check_type(info.params[1]) and info
      elseif tag == 'interface' then
	 return check_type(info.interface) and info
      elseif tag == 'glist' or tag == 'gslist' then
	 return check_type(info.params[1]) and info
      elseif tag == 'ghash' then
	 return ((check_type(info.params[1]) and check_type(info.params[2]))
	      and info)
      elseif tag == 'error' then
	 return info
      else
	 log.warning('unknown typetag %s', tag)
	 return nil
      end
   elseif info.is_callable then
      -- Check all callable arguments and return value.
      if not check_type(info.return_type) then return nil end
      local args = info.args
      for i = 1, #args do
	 if not check_type(args[i].typeinfo) then return nil end
      end
      return info
   elseif type == 'constant' or type == 'property' or type == 'field' then
      return check_type(info.typeinfo) and info
   elseif info.is_registered_type then
      -- Check, whether we can reach the symbol in the repo.
      local ns, n = info.namespace, info.name
      return (in_load[ns .. '.' .. n] or repo[ns][n]) and info
   else
      log.warning("unknown type `%s' of %s", type, info.fullname)
      return nil
   end
end

typeloader['function'] =
   function(namespace, info)
      return check_type(info) and core.construct(info), '_functions'
   end

typeloader['constant'] =
   function(namespace, info)
      return check_type(info) and core.construct(info), '_constants'
   end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   local values = info.values
   for i = 1, #values do
      local mi = values[i]
      value[mi.name:upper()] = mi.value
   end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

typeloader['enum'] =
   function(namespace, info)
      return load_enum(info, enum_mt), '_enums'
   end

typeloader['flags'] =
   function(namespace, info)
      return load_enum(info, bitflags_mt), '_enums'
   end

-- Gets table for category of compound (i.e. _fields of struct or _properties
-- for class etc).  Installs metatable which performs on-demand lookup of
-- symbols.
local function get_category(children, xform_value, xform_name,
			    xform_name_reverse, original_table)
   assert(not xform_name or xform_name_reverse)

   -- Early shortcircuit; no elements, no table needed at all.
   if #children == 0 then return original_table end

   -- Index contains array of indices which were still not retrieved
   -- from 'children' table, and table part contains name->index
   -- mapping.
   local index = {}
   for i = 1, #children do index[i] = i end
   return setmetatable(
      original_table or {}, { __index =
	    function(category, req_name)
	       -- Querying index 0 has special semantics; makes the
	       -- whole table fully loaded.
	       if req_name == 0 then
		  local ei, en, val

		  -- Load al values from unknown indices.
		  while #index > 0 do
		     ei = check_type(children[table.remove(index)])
		     val = ei and (not xform_value and ei or xform_value(ei))
		     if val then
			en = ei.name
			if xform_name_reverse then
			   en = xform_name_reverse(en, ei)
			end
			if en then category[en] = val end
		     end
		  end

		  -- Load all known indices.
		  for en, idx in pairs(index) do
		     val = check_type(children[idx])
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
		  val = children[idx]
		  index[name] = nil
	       else
		  -- Not yet, go through unknown indices and try to
		  -- find the name.
		  while #index > 0 do
		     idx = table.remove(index)
		     val = children[idx]
		     local en = val.name
		     if en == name then break end
		     val = nil
		     index[en] = idx
		  end
	       end

	       -- If there is nothing in the index, we can disconnect
	       -- metatable, because everything is already loaded.
	       if not next(index) then
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

-- Sets up compound header (field 0) and metatable for the compound table.
local function load_compound(compound, info, mt)
   -- Fill in meta of the compound.
   if info.gtype then
      compound._gtype = info.gtype
      repo[compound._gtype] = compound
   end
   compound._name = info.fullname
   compound._resolve = resolve_compound
   setmetatable(compound, mt)
end

local function load_element_property(pi)
   return function(obj, mode, newval)
	     if mode == nil then return pi end
	     if mode then
		return core.object.property(obj, pi, newval)
	     else
		return core.object.property(obj, pi)
	     end
	  end
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
   function(obj, mode, newval)
      if mode == nil then return info end
      if mode then
	 -- Assignment means 'connect signal without detail'.
	 core.object.connect(obj, info.name, info, newval)
      else
	 -- Reading yields table with signal operations.
	 local pad = {
	    connect = function(_, target, detail, after)
			 return core.object.connect(obj, info.name, info,
						    target, detail, after)
		      end,
	 }

	 -- If signal supports details, add metatable implementing __newindex
	 -- for connecting in the 'on_signal['detail'] = handler' form.
	 if info.is_signal and info.flags.detailed then
	    setmetatable(pad, {
			    __newindex =
			       function(obj, detail, target)
				  core.object.connect(obj, info.name,
						      info, newval, detail)
			       end
			 })
	 end

	 -- Return created signal pad.
	 return pad
      end
   end
end

local function load_field(fi, field_accessor)
   -- Check the type of the field.
   local ii = fi.typeinfo.interface
   if ii and (ii.type == 'struct' or ii.type == 'union') then
      -- Nested structure, handle assignment to it specially.
      return function(obj, mode, newval)
		-- If reading the type, read it directly.
		if mode == nil then return fi end

		-- Get access to underlying nested structure.
		local sub = field_accessor(obj, fi)

		-- Reading it is simple, we are done.
		if not mode then return sub end

		-- Writing means assigning all fields from the source
		-- table.
		for name, value in pairs(newval) do sub[name] = value end
	     end
   end

   -- For other types, simple closure around elementof() is sufficient.
   return function(obj, mode, newval)
	     if mode == nil then return fi end
	     if mode then
		return field_accessor(obj, fi, newval)
	     else
		return field_accessor(obj, fi)
	     end
	  end
end

local function load_record_field(fi)
   return load_field(fi, core.record.field)
end

local function load_object_field(fi)
   return load_field(fi, core.object.field)
end

-- _access method for structs.
function struct_mt:_access(instance, name, ...)
   local setmode = select('#', ...) > 0
   local val = self[name]
   if val == nil then error(("%s: no `%s'"):format(self._name, name)) end
   if type(val) ~= 'function' then
      assert(not setmode, ("%s: `s' is not writable"):format(self._name, name))
      return val
   end
   return val(instance, setmode, ...)
end

-- _access method for objects.
function class_mt:_access(instance, name, ...)
   local setmode = select('#', ...) > 0
   local val = self[name]
   if val == nil then
      -- Check for dynamic interfaces.
      for _, iinfo in pairs(core.object.interfaces(instance)) do
	 itype = repo[iinfo.namespace][iinfo.name]
	 val = itype and itype[name]
	 if val then break end
      end

      if not val then
	 -- Check for dynamic properties.
	 local prop = core.object.properties(instance, name:gsub('_', '%-'))
	 if prop then return core.object.property(instance, prop, ...) end
	 error(("%s: no `%s'"):format(self._name, name))
      end
   end

   if type(val) == 'function' then
      return val(instance, setmode, ...)
   else
      assert(not setmode, ("%s: `s' is not writable"):format(self._name, name))
      return val
   end
end

-- Loads structure information into table representing the structure
local function load_struct(namespace, struct, info)
   -- Avoid exposing internal structs created for object implementations.
   if not info.is_gtype_struct then
      load_compound(struct, info, struct_mt)
      struct._methods = get_category(
	 info.methods, core.construct, nil, nil, rawget(struct, '_methods'))
      struct._fields = get_category(info.fields, load_record_field)
      struct._access = struct_access
   end
end

typeloader['struct'] =
   function(namespace, info)
      local struct = {}
      load_struct(namespace, struct, info)
      return struct, '_structs'
   end

typeloader['union'] =
   function(namespace, info)
      local union = {}
      load_struct(namespace, union, info)
      return union, '_unions'
   end

typeloader['interface'] =
   function(namespace, info)
      -- Load all components of the interface.
      local interface = {}
      load_compound(interface, info, interface_mt)
      interface._properties = get_category(
	 info.properties, load_element_property,
	 function(name) return string.gsub(name, '_', '%-') end,
	 function(name) return string.gsub(name, '%-', '_') end)
      interface._methods = get_category(
	 info.methods,
	 function(ii)
	    local flags = ii.flags
	    if not flags.is_getter and not flags.is_setter then
	       return core.construct(ii)
	    end
	 end) or {}
      -- If method is not found normal way, try to look it up in
      -- parent namespace (e.g. g_file_new_for_path is exported in
      -- typelib as Gio.file_new_for_path, while we really want it
      -- accessible as Gio.File.new_for_path).
      local meta = getmetatable(interface._methods) or {}
      local old_index = meta.__index
      function meta:__index(symbol)
	 local val
	 if old_index then val = old_index(self, symbol) end
	 if not val then
	    -- Convert name from CamelCase to underscore_delimited form.
	    local method_name = {}
	    for part in info.name:gmatch('%u%l*') do
	       method_name[#method_name + 1] = part:lower()
	    end
	    method_name[#method_name + 1] = symbol
	    val = namespace[table.concat(method_name, '_')]
	    self[symbol] = val
	 end
	 return val
      end
      setmetatable(interface._methods, meta)
      interface._signals = get_category(
	 info.signals, load_element_signal,
	 load_signal_name, load_signal_name_reverse)
      interface._constants = get_category(info.constants, core.construct)
      return interface, '_interfaces'
   end

-- Loads structure information into table representing the structure
local function load_class(namespace, class, info)
   -- Load components of the object.
   load_compound(class, info, class_mt)
   class._properties = get_category(
      info.properties, load_element_property,
      function(n) return (string.gsub(n, '_', '%-')) end,
      function(n) return (string.gsub(n, '%-', '_')) end)
   class._methods = get_category(
      info.methods,
      function(mi)
	 local flags = mi.flags
	 if not flags.is_getter and not flags.is_setter then
	    return core.construct(mi)
	 end
      end, nil, nil, rawget(class, '_methods'))
   class._signals = get_category(
      info.signals, load_element_signal, load_signal_name,
      load_signal_name_reverse)
   class._constants = get_category(info.constants, core.construct)
   class._implements = get_category(
      info.interfaces,
      function(ii) return repo[ii.namespace][ii.name] end,
      nil,
      function(n, ii) return ii.namespace .. '.' .. n end)
   class._fields = get_category(info.fields, load_object_field)
   local _ = rawget(class, '_inherits') and class._inherits[0]

   -- Add parent (if any) into _inherits table.
   local parent = info.parent
   if parent then
      local ns, name = parent.namespace, parent.name
      if ns ~= namespace[0].name or name ~= info.name then
	 class._parent = repo[ns][name]
      end
   end
end

typeloader['object'] =
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

   -- Lookup baseinfo of requested symbol in the GIRepository.
   local info = gi[namespace[0].name][symbol]
   if not info then return nil end

   -- Store the symbol into the in-load table, because we have to
   -- avoid infinte recursion which might happen during symbol loading (mainly
   -- when prerequisity of the interface is the class which implements said
   -- interface).
   in_load[info.fullname] = info

   -- Decide according to symbol type what to do.
   if info and not info.deprecated then
      local loader = typeloader[info.type]
      if loader then
	 local category
	 value, category = loader(namespace, info)

	 -- Cache the symbol in specified category in the namespace.
	 local cat = rawget(namespace, category) or {}
	 namespace[category] = cat
	 cat[symbol] = value
      end
   end

   in_load[info.fullname] = nil
   return value
end

-- Resolves everything in the namespace by iterating through it.
local function resolve_namespace(ns_meta)
   -- Iterate through all items in the namespace and dereference them,
   -- which causes them to be loaded in and cached inside the namespace
   -- table.
   local name = ns_meta.name
   local ns = repo[name]
   for i = 1, #gi[name] do
      pcall(function() local _ = ns[gi[name][i].name] end)
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

   -- Load the typelibrary for the namespace.
   local ns = gi.require(name)
   if not ns then return nil end

   -- Create _meta table containing auxiliary information and data for
   -- the namespace.
   setmetatable(into, namespace_mt)
   into[0] = { name = name, dependencies = {} }
   into[0].version = ns.version
   into[0].dependencies = ns.dependencies

   -- Install 'resolve' closure, which forces loading this namespace.
   -- Useful when someone wants to inspect what's inside (e.g. some
   -- kind of source browser or smart editor).
   into[0].resolve = resolve_namespace

   -- Make sure that all dependent namespaces are also loaded.
   for name, version in pairs(into[0].dependencies or {}) do
      local _ = repo[name]
   end

   return into
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(repo, name)
				  return load_namespace(nil, name)
			       end })

-- GObject modifications.
do
   local obj = repo.GObject.Object

   -- No methods are needed (yet).
   obj._methods = nil

   -- Install custom _element handler, which is used to handle dynamic
   -- properties (the ones which are present on the real instance, but
   -- not in the typelibs).
   function obj._element(type, obj, name)
      local element = type[name]
      if not element then
	 -- List all interfaces implemented by this object and try
	 -- whether they can handle specified _element request.
	 local interfaces = core.interfaces(obj)
	 for i = 1, #interfaces do
	    local interface_type =
	       repo[interfaces[i].namespace][interfaces[i].name]
	    if interface_type then
	       element = interface_type:_element(obj, name)
	       if element then return element end
	    end
	 end

	 -- Element not found in the repo (typelib), try whether
	 -- dynamic property of the specified name exists.
	 local pspec = core.properties(obj, string.gsub(name, '_', '%-'))
	 if pspec then
	    element = function(obj, _, mode, newval)
			 return core.elementof(obj, pspec, mode, newval)
		      end
	 end
      end
      return element
   end

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
	 core.connect(obj, info.name, info, get_notifier(newval))
      else
	 -- Reading yields table with signal operations.
	 local pad = {
	    connect = function(_, target, property)
			 return core.connect(obj, info.name, info,
					     get_notifier(target),
					     property.name)
		      end,
	 }

	 -- Add metatable allowing connection on specified detail.  Detail is
	 -- always specified as a property for this signal.
	 setmetatable(pad, {
			 __newindex = function(_, property, target)
					 core.connect(obj, info.name,
						      info,
						      get_notifier(target),
						      property.name)
				      end
			 })

	 -- Return created signal pad.
	 return pad
      end
   end

   -- Install 'notify' signal.  Unfortunately typelib does not contain its
   -- declaration, so we borrow it from callback GObject.ObjectClass.notify.
   obj._signals = {}
   function obj._signals.on_notify(obj, _, newval)
      return on_notify(
	 obj, gi.GObject.ObjectClass.fields.notify.typeinfo.interface, newval)
   end

   -- ParamSpec.  Manually add its gtype, because it is not present in
   -- the typelib and is vital to dynamic elementof() innards.
   repo.GObject.ParamSpec._gtype = repo.GObject.type_from_name('GParam')

   -- Closure modifications.  Closure does not need any methods nor
   -- fields, but it must have constructor creating it from any kind
   -- of Lua callable.
   local closure = repo.GObject.Closure
   local closure_info = gi.GObject.Closure
   closure._methods = nil
   closure._fields = nil
   local closure_mt = { __index = getmetatable(closure).__index,
			__tostring = getmetatable(closure).__tostring }
   function closure_mt:__call(arg)
      return core.record.new(closure_info, arg)
   end
   setmetatable(closure, closure_mt)

   -- Implicit conversion constructor, allows using Lua function
   -- directly at the places where GClosure is expected.
   function closure:_construct(arg)
      return core.record.new(closure_info, arg)
   end

   -- Value is constructible from any kind of source Lua
   -- value, and the type of the value can be hinted by type name.
   local value = repo.GObject.Value
   local value_info = gi.GObject.Value

   -- Tries to deduce the gtype according to Lua value.
   local function gettype(source)
      if source == nil then return 'void'
      elseif type(source) == 'boolean' then return 'gboolean'
      elseif type(source) == 'number' then
	 -- If the number fits in integer, use it, otherwise use double.
	 local _, fract = math.modf(source)
	 local maxint32 = 0x80000000
	 return ((fract == 0 and source >= -maxint32 and source < maxint32)
	      and 'gint' or 'gdouble')
      elseif type(source) == 'string' then return 'gchararray'
      elseif type(source) == 'function' then return closure._gtype
      elseif type(source) == 'userdata' then
	 -- Check whether is it record or object.
	 local typetable, gtype = core.record.typeof(source)
	 if typetable then return typetable._gtype end
	 typetable, gtype = core.object.typeof(source)
	 if typetable then return gtype end

	 -- Check whether we can call this userdata.  If yes, generate
	 -- closure.
	 local meta = getmetatable(source)
	 if meta and meta.__call then return closure._gtype end
      elseif type(source) == 'table' then
	 -- Check, whether we can call it.
	 local meta = getmetatable(source)
	 if meta and meta.__call then return closure._gtype end
      end

      -- No idea to what type should this be mapped.
      error(("unclear type for GValue from argument `%s'"):format(
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
      return core.record.new(value_info, stype, source)
   end
   setmetatable(value, value_mt)
   value._construct = value_mt.__call
   value._methods = nil
   value._fields = nil
   function value:_access(instance, name, ...)
      assert(select('#', ...) == 0,
	     ("GObject.Value: `%s' not writable"):format(name));
      if name == 'type' then
	 return repo.GObject.type_name(
	    core.record.field(instance, value_info.fields.g_type)) or ''
      elseif name == 'value' then
	 return core.record.valueof(instance)
      end
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
setmetatable(lgi, { __index = function(_, name) return repo[name] end })
return lgi
