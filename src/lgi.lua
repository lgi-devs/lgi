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

-- Gets table for category of compound (i.e. _fields of struct or _properties
-- for class etc).  Installs metatable which performs on-demand lookup of
-- symbols.
local function get_category(children, xform_value,
			    xform_name, xform_name_reverse)
   -- Either none or both transform methods must be provided.
   assert(not xform_name or xform_name_reverse)

   -- Early shortcircuit; no elements, no table needed at all.
   if #children == 0 then return nil end

   -- Index contains array of indices which were still not retrieved
   -- from 'children' table, and table part contains name->index
   -- mapping.
   local index, mt = {}, {}
   for i = 1, #children do index[i] = i end

   -- Fully resolves the category (i.e. loads everything remaining to
   -- be loaded in given category) and disconnects on-demand loading
   -- metatable.
   local function resolve(category)
      -- Load al values from unknown indices.
      local ei, en, val
      while #index > 0 do
	 ei = check_type(children[table.remove(index)])
	 val = ei and (not xform_value and ei or xform_value(ei))
	 if val then
	    en = ei.name
	    if xform_name_reverse then
	       en = xform_name_reverse(en, ei)
	    end
	    if en then self[en] = val end
	 end
      end

      -- Load all known indices.
      for en, idx in pairs(index) do
	 val = check_type(children[idx])
	 val = not xform_value and val or xform_value(val)
	 self[en] = val
      end

      -- Metatable is no longer needed, disconnect it.
      setmetatable(self, nil)
   end

   function mt:__index(requested_name)
      -- Check if closure for fully resolving the category is needed.
      if requested_name == '_resolve' then
	 return resolve
      end

      -- Transform name by transform function.
      local name = not xform_name and requested_name
	 or xform_name(requested_name)
      if not name then return end

      -- Check, whether we already know its index.
      local idx, val = index[name]
      if idx then
	 -- We know at least the index, so get info directly.
	 val = children[idx]
	 index[name] = nil
      else
	 -- Not yet, go through unknown indices and try to find the
	 -- name.
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
	 setmetatable(self, nil)
      end

      -- Transform found value and store it into the category (self)
      -- table.
      if not check_type(val) then return nil end
      if xform_value then val = xform_value(val) end
      if not val then return nil end
      self[requested_name] = val
      return val
   end
   return setmetatable({}, mt)
end

-- Loads element from specified compound typetable.
local function get_element(typetable, symbol)
   -- Decompose symbol name, in case that it contains category prefix
   -- (e.g. '_field_name' when requesting explicitely field called
   -- name).
   local category, name = string.match(symbol, '^(_.-)_(.*)$')
   if category and name then
      -- Check requested category.
      local cat = rawget(typetable, category)
      local val = cat and cat[name]
      if val then return val end
   elseif string.sub(symbol, 1, 1) ~= '_' then
      -- Check all available categories.
      local categories = typetable._categories or {}
      for i = 1, #categories do
	 local cat = rawget(typetable, categories[i])
	 local val = cat and cat[symbol]
	 if val then return val end
      end
   end

   -- Check parent and all implemented interfaces.
   local parent = rawget(typetable, '_parent')
   val = parent and parent[symbol]
   if val then return val end
   local implements = rawget(typetable, '_implements')
   if implements then
      for _, implemented in pairs(implements) do
	 val = implemented[symbol]
	 if val then return val end
      end
   end

   -- As a last resort, metatable might contain fallback implementation.
   local meta = getmetatable(typetable)
   return meta and meta[symbol]
end

-- Fully resolves the whole typetable, i.e. load all symbols normally
-- loaded on-demand at once.
local function resolve_elements(typetable)
   local categories = typetable._categories or {}
   for i = 1, #categories do
      local category = rawget(typetable, categories[i])
      local resolve = type(category) == 'table' and category._resolve
      local _ = resolve and resolve(typetable)
   end
end

-- Metatables for assorted repo components.
local function create_component_meta(categories)
   return { _categories = categories, _resolve = resolve_elements,
	    __index = get_element }
end

local component_mt = {
   namespace = create_component_meta {
      '_classes', '_interfaces', '_structs', '_unions', '_enums',
      '_functions', '_constants', },
   record = create_component_meta {
      '_methods', '_fields'
   },
   interface = create_component_meta {
      '_properties', '_methods', '_signals', '_constants' },
   class = create_component_meta {
      '_properties', '_methods', '_signals', '_constants', '_fields' },
   bitflags = {},
   enum = {},
}

-- Resolving arbitrary number to the table containing symbolic names
-- of contained bits.
function component_mt.bitflags:__index(value)
   local t = {}
   for name, flag in pairs(self) do
      if type(flag) == 'number' and value % (2 * flag) >= flag then
	 t[name] = flag
      end
   end
   return t
end

-- Implements reverse mapping, value->name.
function component_mt.enum:__index(value)
   for name, val in pairs(self) do
      if val == value then return name end
   end
end

-- _access method for records and simple objects.
local function record_access(self, instance, name, ...)
   local val = self[name]
   if val == nil then error(("%s: no `%s'"):format(self._name, name)) end
   if type(val) ~= 'function' then
      assert(select('#', ...) == 0,
	     ("%s: `s' is not writable"):format(self._name, name))
      return val
   end
   return val(instance, ...)
end

component_mt.record._access = record_access
component_mt.class._access = record_access

-- If member of interface cannot be found normally, try to look it up
-- in parent namespace too; this allows to overcome IMHO gir-compiler
-- flaw, causing that we can see e.g. Gio.file_new_for_path() instead
-- of Gio.File.new_for_path().
function component_mt.interface:__index(symbol)
   local val = get_element(self, symbol)
   if not val then
      -- Convert name from CamelCase to underscore_delimited form.
      local method_name = {}
      for part in info.name:gmatch('%u%l*') do
	 method_name[#method_name + 1] = part:lower()
      end
      method_name[#method_name + 1] = symbol
      val = repo[self._name:gsub('([%w_])')][table.concat(method_name, '_')]
      self[symbol] = val
   end
   return val
end

-- Create structure instance and initialize it with given fields.
function component_mt.record:__call(fields)
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

-- Object constructor, 'param' contains table with properties/signals
-- to initialize.
function component_mt.class:__call(args)
   -- Process 'param' table, separate properties from other fields.
   local props, others = {}, {}
   for name, value in pairs(args or {}) do
      local argtype = self[name]
      if type(paramtype) == 'userdata' and paramtype.is_property then
	 props[paramtype] = value
      else
	 others[name] = value
      end
   end

   -- Create the object.
   local obj = core.object.new(self._gtype, props)

   -- Attach signals previously filtered out from creation.
   for name, func in pairs(others) do obj[name] = func end
   return obj
end

-- Weak table containing symbols which currently being loaded. It is used
-- mainly to avoid assorted kinds of infinite recursion which might happen
-- during symbol dependencies loading.
local in_check = setmetatable({}, { __mode = 'v' })

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
   elseif type == 'constant' or type == 'property' or type == 'field' then
      if not check_type(info.typeinfo) then info = nil end
   elseif info.is_registered_type then
      -- Check, whether we can reach the symbol in the repo.
      in_check[info.fullname] = info
      if not in_check[info.fullname]
	 and not repo[info.namespace][info.name] then
	 info = nil
      end
      in_check[info.fullname] = nil
   else
      log.warning("unknown type `%s' of %s", type, info.fullname)
      return nil
   end
end

-- Creates new component and sets up common parts according to given
-- info.
local function create_component(info, mt)
   -- Fill in meta of the compound.
   local component = { _name = info.fullname }
   if info.gtype then
      component._gtype = info.gtype
      repo[rawget(component, _gtype)] = component
   end
   return setmetatable(compound, mt)
end

-- Table containing loaders for various GI types, indexed by
-- gi.InfoType constants.
local typeloader = {}

typeloader['function'] =
   function(namespace, info)
      return check_type(info) and core.callable.new(info), '_functions'
   end

function typeloader.constant(namespace, info)
   return check_type(info) and core.constant(info), '_constants'
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

function typeloader.enum(namespace, info)
   return load_enum(info, component_mt.enum), '_enums'
end

function typeloader.flags(namespace, info)
   return load_enum(info, component_mt.bitflags), '_enums'
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
   function(obj, ...)
      if select('#', ...) > 0 then
	 -- Assignment means 'connect signal without detail'.
	 core.object.connect(obj, info.name, info, ...)
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
      return function(obj, ...)
		-- Get access to underlying nested structure.
		local sub = field_accessor(obj, fi)

		-- Reading it is simple, we are done.
		if select('#', ...) == 0 then return sub end

		-- Writing means assigning all fields from the source
		-- table.
		for name, value in pairs(newval) do sub[name] = value end
	     end
   end

   -- For other types, simple closure around elementof() is sufficient.
   return function(obj, ...) return field_accessor(obj, fi, ...) end
end

local function load_method(mi)
   local flags = mi.flags
   if not flags.is_getter and not flags.is_setter then
      return core.callable.new(mi)
   end
end

local function load_record_field(fi)
   return load_field(fi, core.record.field)
end

local function load_object_field(fi)
   return load_field(fi, core.object.field)
end

-- Loads structure information into table representing the structure
local function load_record(namespace, info)
   -- Avoid exposing internal structs created for object implementations.
   if not info.is_gtype_struct then
      local record = create_component(info, component_mt.record)
      record._methods = get_category(info.methods, core.callable.new)
      record._fields = get_category(info.fields, load_record_field)
   end
end

function typeloader.struct(namespace, info)
   return load_record(namespace, info), '_structs'
end

function typeloader.union(namespace, info)
   return load_record(namespace, info), '_unions'
end

local function load_properties(info)
   return get_category(
      info.properties, nil,
      function(name) return string.gsub(name, '_', '%-') end,
      function(name) return string.gsub(name, '%-', '_') end)
end

function typeloader.interface(namespace, info)
   -- Load all components of the interface.
   local interface = create_component(info, component_mt.interface)
   interface._properties = load_properties(info)
   interface._methods = get_category(info.methods, load_method)
   interface._signals = get_category(
      info.signals, load_element_signal,
      load_signal_name, load_signal_name_reverse)
   interface._constants = get_category(info.constants, core.constant)
   return interface, '_interfaces'
end

function typeloader.object(namespace, info)
   local class = create_component(info, component_mt.class)
   class._properties = load_properties(info)
   class._methods = get_category(info.methods, load_method)
   class._signals = get_category(
      info.signals, load_element_signal, load_signal_name,
      load_signal_name_reverse)
   class._constants = get_category(info.constants, core.constant)
   class._fields = get_category(info.fields, load_object_field)
   local implements = {}
   for _, interface in pairs(info.interfaces) do
      implements[interface.fullname] =
	 repo[interface.namespace][interface.name]
   end
   class._implements = implements

   -- Add parent (if any) into _inherits table.
   local parent = info.parent
   if parent then
      local ns, name = parent.namespace, parent.name
      if ns ~= namespace._name or name ~= info.name then
	 class._parent = repo[ns][name]
      end
   end
   return class, '_classes'
end

-- Gets symbol of the specified namespace, if not present yet, tries to load it
-- on-demand.
function component_mt.namespace:__index(symbol)
   -- Check, whether symbol is already loaded.
   local val = get_element(self, symbol)
   if val then return val end

   -- Lookup baseinfo of requested symbol in the GIRepository.
   local info = gi[namespace._name][symbol]
   if not info then return nil end

   -- Decide according to symbol type what to do.
   local loader = typeloader[info.type]
   if loader then
      local val, category = loader(namespace, info)

      -- Cache the symbol in specified category in the namespace.
      local cat = rawget(namespace, category) or {}
      namespace[category] = cat
      cat[symbol] = value
   end
   return val
end

-- Resolves everything in the namespace by iterating through it.
function component_mt.namespace:_resolve(deep)
   -- Iterate through all items in the namespace and dereference them,
   -- which causes them to be loaded in and cached inside the namespace
   -- table.
   local gi_ns = gi[self._name]
   for i = 1, #gi_ns do
      local ok, component = pcall(function()
				     return self[gi_ns[i].name]
				  end)
      if ok and deep and component then
	 component:_resolve(deep)
      end
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
   into._name = name
   into._dependencies = ns.dependencies
   into._version = ns.version

   -- Make sure that all dependent namespaces are also loaded.
   for name, version in pairs(into._dependencies or {}) do
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
