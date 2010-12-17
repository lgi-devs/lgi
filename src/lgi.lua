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

-- Map buffer creation from core to Lgi-style 'C++ constructor' convention.
lgi.buffer = setmetatable({ new = core.buffer_new },
			  { __call = function(_, arg)
					return core.buffer_new(arg)
				     end })

-- Prepare logging support.  'log' is module-exported table, containing all
-- functionality related to logging wrapped around GLib g_log facility.
local logtable = { ERROR = 'assert', DEBUG = 'silent' }
lgi.log = logtable
core.set('logger',
	 function(domain, level, message)
	    -- Create domain table in the log table if it does not
	    -- exist yet.
	    if not logtable[domain] then logtable[domain] = {} end

	    -- Check whether message should generate assert (i.e. Lua
	    -- exception).
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

log.message('Lua to GObject-Introspection binding v0.1')

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

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
   return info
end

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
      local function xvalue(arg)
	 if not xform_value then return arg end
	 if arg then
	    local ok, res = pcall(xform_value, arg)
	    return ok and res
	 end
      end
      while #index > 0 do
	 ei = check_type(children[table.remove(index)])
	 val = xvalue(ei)
	 if val then
	    en = ei.name
	    en = not xform_name_reverse and en or xform_name_reverse(en)
	    if en then category[en] = val end
	 end
      end

      -- Load all known indices.
      for en, idx in pairs(index) do
	 val = xvalue(check_type(children[idx]))
	 en = not xform_name_reverse and en or xform_name_reverse(en)
	 if en then category[en] = val end
      end

      -- Metatable is no longer needed, disconnect it.
      return setmetatable(category, nil)
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
local function get_element(typetable, instance, symbol)
   -- Check whether symbol directly exists.
   local val = rawget(typetable, symbol)
   if val then return val end

   -- Decompose symbol name, in case that it contains category prefix
   -- (e.g. '_field_name' when requesting explicitely field called
   -- name).
   local category, name = string.match(symbol, '^(_.-)_(.*)$')
   if category and name then
      -- Check requested category.
      local cat = rawget(typetable, category)
      val = cat and cat[name]
      if val then return val end
   elseif string.sub(symbol, 1, 1) ~= '_' then
      -- Check all available categories.
      local categories = typetable._categories or {}
      for i = 1, #categories do
	 local cat = rawget(typetable, categories[i])
	 val = cat and cat[symbol]
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
local function resolve_elements(typetable, recursive)
   local categories = typetable._categories or {}
   for i = 1, #categories do
      local category = rawget(typetable, categories[i])
      local resolve = type(category) == 'table' and category._resolve
      local _ = resolve and resolve(category)
   end
   if recursive then
      for _, iface in pairs(typetable._implements or {}) do
	 iface:_resolve(recursive)
      end
      if typetable._parent then
	 typetable._parent:_resolve(recursive)
      end
   end
   return typetable
end

-- Default _access method implementation.
local function default_access(typetable, instance, name, ...)
   -- Get element from typetable.
   local element = typetable:_element(instance, name)

   -- Try custom override first.
   if element == nil then
      local func = typetable:_element(instance, '_custom_' .. name)
      if func then return func(typetable, instance, name, ...) end
   end

   -- Forward to access_element implementation.
   return typetable:_access_element(instance, name, element, ...)
end

-- Default _access_element implementation.
local function default_access_element(typetable, instance, name, element, ...)
   if element == nil then
      -- Generic failure.
      error(("%s: no `%s'"):format(typetable._name, name))
   else
      -- Static member, is always read-only when accessing per-instance.
      assert(select('#', ...) == 0,
	     ("%s: `%s' is not writable"):format(typetable._name, name))
      return element
   end
end

-- Metatables for assorted repo components.
local function create_component_meta(categories)
   local meta = {
      _categories = categories,
      _resolve = resolve_elements,
      _element = get_element,
      _access = default_access,
      _access_element = default_access_element }
   function meta:__index(symbol)
      return getmetatable(self)._element(self, nil, symbol)
   end
   return meta
end

local component_mt = {
   namespace = create_component_meta {
      '_classes', '_interfaces', '_structs', '_unions', '_enums',
      '_functions', '_constants', },
   record = create_component_meta {
      '_methods', '_fields'
   },
   interface = create_component_meta {
      '_properties', '_vfuncs', '_methods', '_signals', '_constants' },
   class = create_component_meta {
      '_properties', '_vfuncs', '_methods', '_signals', '_constants',
      '_fields' },
   bitflags = {},
   enum = {},
}

-- Resolving arbitrary number to the table containing symbolic names
-- of contained bits.
function component_mt.bitflags:__index(value)
   if type(value) ~= 'number' then return end
   local t = {}
   for name, flag in pairs(self) do
      if type(flag) == 'number' and value % (2 * flag) >= flag then
	 t[name] = flag
      end
   end
   return t
end

-- Enum reverse mapping, value->name.
function component_mt.enum:__index(value)
   for name, val in pairs(self) do
      if val == value then return name end
   end
end

-- _access part for fields.
local function access_field(field_accessor, instance, info, ...)
   -- Check the type of the field.
   local ii = info.typeinfo.interface
   if ii and (ii.type == 'struct' or ii.type == 'union') then
      -- Nested structure, handle assignment to it specially.  Get
      -- access to underlying nested structure.
      local subrecord = field_accessor(instance, info)

      -- Reading it is simple, we are done.
      if select('#', ...) == 0 then return subrecord end

      -- Writing means assigning all fields from the source table.
      for name, value in pairs(...) do subrecord[name] = value end
   else
      -- For other types, simple closure around elementof() is
      -- sufficient.
      return field_accessor(instance, info, ...)
   end
end

-- _access part for signals.
local function access_signal(instance, info, ...)
   if select('#', ...) > 0 then
      -- Assignment means 'connect signal without detail'.
      core.object.connect(instance, info.name, info, ...)
   else
      -- Reading yields table with signal operations.
      local pad = {}
      function pad:connect(target, detail, after)
	 return core.object.connect(instance, info.name, info,
				    target, detail, after)
      end

      -- If signal supports details, add metatable implementing
      -- __newindex for connecting in the 'on_signal['detail'] =
      -- handler' form.
      if info.is_signal and info.flags.detailed then
	 local meta = {}
	 function meta:__newindex(detail, target)
	    core.object.connect(instance, info.name, info, target, detail)
	 end
	 setmetatable(pad, meta);
      end

      -- Return created signal pad.
      return pad
   end
end

-- _access_element method for records.
function component_mt.record:_access_element(instance, name, element, ...)
   if gi.isinfo(element) and element.is_field then
      return access_field(core.record.field, instance, element, ...)
   end
   return default_access_element(self, instance, name, element, ...)
end

-- _access_element method for raw objects (fundamentals).  Specific
-- behavior for GObject will be overriden in GObject.Object._access().
function component_mt.class:_access_element(instance, name, element, ...)
   if gi.isinfo(element) then
      if element.is_field then
	 return access_field(core.object.field, instance, element, ...)
      elseif element.is_signal then
	 return access_signal(instance, element, ...)
      elseif element.is_vfunc then
	 local typetable, _, typestruct = core.object.typeof(
	    instance, element.container.gtype)
	 return core.record.field(typestruct, typetable._type[element.name])
      end
   end
   return default_access_element(self, instance, name, element, ...)
end

-- If member of interface cannot be found normally, try to look it up
-- in parent namespace too; this allows to overcome IMHO gir-compiler
-- flaw, causing that we can see e.g. Gio.file_new_for_path() instead
-- of Gio.File.new_for_path().
function component_mt.interface:_element(instance, symbol)
   local val = get_element(self, instance, symbol)
   if not val then
      -- Convert name from CamelCase to underscore_delimited form.
      local ns_name, iface_name = self._name:match('^([%w_]+)%.([%w_]+)$')
      local method_name = {}
      for part in iface_name:gmatch('[%u%d][%l%d]*') do
	 method_name[#method_name + 1] = part:lower()
      end
      method_name[#method_name + 1] = symbol
      val = repo[ns_name][table.concat(method_name, '_')]
      self[symbol] = val
   end
   return val
end

-- Create structure instance and initialize it with given fields.
function component_mt.record:__call(fields)
   -- Create the structure instance.
   local info
   if self._gtype then
      -- Try to lookup info by gtype.
      info = gi[self._gtype]
   end
   if not info then
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
   -- Process 'args' table, separate properties from other fields.
   local props, others = {}, {}
   for name, value in pairs(args or {}) do
      local argtype = self[name]
      if gi.isinfo(argtype) and argtype.is_property then
	 props[argtype] = value
      else
	 others[name] = value
      end
   end

   -- Create the object.
   local object = core.object.new(self._gtype, props)

   -- Attach signals previously filtered out from creation.
   for name, func in pairs(others) do object[name] = func end
   return object
end

-- Creates new component and sets up common parts according to given
-- info.
local function create_component(info, mt)
   -- Fill in meta of the compound.
   local component = { _name = info.fullname or info.name }
   if info.gtype then
      component._gtype = info.gtype
      repo[rawget(component, '_gtype')] = component
   end
   return setmetatable(component, mt)
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
   name = name:match('^on_(.+)$')
   return name and name:gsub('_', '%-')
end

local function load_signal_name_reverse(name)
   return 'on_' .. name:gsub('%-', '_')
end

local function load_vfunc_name(name)
   return name:match('^on_(.+)$')
end

local function load_vfunc_name_reverse(name)
   return 'on_' .. name
end

local function load_method(mi)
   local flags = mi.flags
   if not flags.is_getter and not flags.is_setter then
      return core.callable.new(mi)
   end
end

-- Loads structure information into table representing the structure
local function load_record(namespace, info)
   -- Avoid exposing internal structs created for object implementations.
   if not info.is_gtype_struct then
      local record = create_component(info, component_mt.record)
      record._methods = get_category(info.methods, core.callable.new)
      record._fields = get_category(info.fields)
      return record
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
   interface._signals = get_category(info.signals, nil, load_signal_name,
				     load_signal_name_reverse)
   interface._constants = get_category(info.constants, core.constant)
   local type_struct = info.type_struct
   if type_struct then
      interface._vfuncs = get_category(info.vfuncs, nil, load_vfunc_name,
				       load_vfunc_name_reverse)
      interface._type = get_category(type_struct.fields)
   end
   return interface, '_interfaces'
end

function typeloader.object(namespace, info)
   local class = create_component(info, component_mt.class)
   class._properties = load_properties(info)
   class._methods = get_category(info.methods, load_method)
   class._signals = get_category(info.signals, nil,
				 load_signal_name, load_signal_name_reverse)
   class._constants = get_category(info.constants, core.constant)
   class._fields = get_category(info.fields)
   local type_struct = info.type_struct
   if type_struct then
      class._vfuncs = get_category(info.vfuncs, nil, load_vfunc_name,
				   load_vfunc_name_reverse)
      class._type = type_struct and get_category(type_struct.fields)
   end

   -- Populate inheritation information (_implements and _parent fields).
   local interfaces, implements = info.interfaces, {}
   for i = 1, #interfaces do
      local iface = interfaces[i]
      implements[iface.fullname] = repo[iface.namespace][iface.name]
   end
   class._implements = implements
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
   local val = get_element(self, nil, symbol)
   if val then return val end

   -- Lookup baseinfo of requested symbol in the GIRepository.
   local info = gi[self._name][symbol]
   if not info then return nil end

   -- Decide according to symbol type what to do.
   local loader = typeloader[info.type]
   if loader then
      local category
      val, category = loader(self, info)

      -- Cache the symbol in specified category in the namespace.
      local cat = rawget(self, category) or {}
      self[category] = cat
      cat[symbol] = val
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
      local ok, component = pcall(function() return self[gi_ns[i].name] end)
      if ok and deep and type(component) == 'table' then
	 local resolve = component._resolve
	 if resolve then resolve(component, deep) end
      end
   end
   return self
end

-- Makes sure that the namespace (optionally with requested version)
-- is properly loaded.
function lgi.require(name, version)
   -- Load the namespace info for GIRepository.
   local ns_info = assert(gi.require(name, version))

   -- If the repository table does not exist yet, create it.
   local ns = rawget(repo, name)
   if not ns then
      ns = create_component(ns_info, component_mt.namespace)
      ns._version = ns_info.version
      ns._dependencies = ns_info.dependencies
      repo[name] = ns

      -- Make sure that all dependent namespaces are also loaded.
      for name, version in pairs(ns._dependencies or {}) do
	 local _ = repo[name]
      end

      -- Try to load override, if it is present.
      local lgix_name = 'lgix-' .. ns._name
      local ok, msg = pcall(require, lgix_name)
      if not ok then
	 -- Try parsing message; if it is something different than
	 -- "module xxx not found", then rethrow the exception.
	 assert(msg:find("module '" .. lgix_name .. "' not found:", 1, true),
		msg)
      end
   else
      assert(not version or ns._version == version,
	     ("loading '%s-%s', but version '%s' is already loaded"):format(
	  ns._name, version, ns._version))
   end
   return ns
end

-- Install metatable into repo table, so that on-demand loading works.
local repo_mt = {}
function repo_mt:__index(name)
   return lgi.require(name)
end
setmetatable(repo, repo_mt)

-- Add gtypes to important GLib and GObject structures, for which the
-- typelibs do not contain them.
for gtype_name, gi_name in pairs {
      GDate = 'GLib.Date', GRegex = 'GLib.Regex', GDateTime = 'GLib.DateTime',
      GVariantType = 'GLib.VariantType', GParam = 'GObject.ParamSpec',
} do
   local gtype = repo.GObject.type_from_name(gtype_name)
   local ns, name = gi_name:match('^([%w_]+)%.([%w_]+)$')
   local gi_type = repo[ns][name]
   gi_type._gtype = gtype
   repo[gtype] = gi_type
end

-- GObject overrides.
local object = repo.GObject.Object

-- Custom _element implementation, checks dynamically inherited
-- interfaces and dynamic properties.
function object:_element(instance, name)
   local element = component_mt.class._element(self, instance, name)
   if element then return element end

   -- List all interfaces implemented by this object and try whether
   -- they can handle specified _element request.
   local interfaces = core.object.interfaces(instance)
   for i = 1, #interfaces do
      local iface = repo[interfaces[i].namespace][interfaces[i].name]
      element = iface and iface:_element(instance, name)
      if element then return element end
   end

   -- Element not found in the repo (typelib), try whether dynamic
   -- property of the specified name exists.
   local pspec = core.object.properties(instance, name:gsub('_', '%-'))
   if pspec then return pspec end
end

-- Custom access_element, reacts on dynamic properties
function object:_access_element(instance, name, element, ...)
   if element == nil then
      -- Check object's environment.
      local env = core.object.env(instance)
      if not env then error(("%s: no `%s'"):format(self._name, name)) end
      if select('#', ...) > 0 then
	 env[name] = ...
	 return
      else
	 return env[name]
      end
   elseif (core.record.typeof(element) == repo.GObject.ParamSpec
	or (gi.isinfo(element) and element.is_property)) then
      -- Get value of property.
      return core.object.property(instance, element, ...)
   else
      -- Forward to 'inherited' generic object implementation.
      return component_mt.class._access_element(
	 self, instance, name, element, ...)
   end
end

-- Install 'on_notify' signal.  There are a few gotchas causing that the
-- signal is handled separately from common signal handling above:
-- 1) There is no declaration of this signal in the typelib.
-- 2) Real notify works with glib-style property names as details.  We
--    use modified property names ('-' -> '_').
-- 3) notify handler gets pspec, but we pass Lgi-mangled property name
--    instead.

-- Real on_notify handler, converts its parameter from pspec to
-- Lgi-style property name.
local function get_notifier(target)
   return function(obj, pspec)
	     return target(obj, (pspec.name:gsub('%-', '_')))
	  end
end

-- Install 'notify' signal.
object._signals = {}
local _ = object._vfuncs.on_notify
object._vfuncs.on_notify = nil
object._custom = {}
function object._custom.on_notify(typetable, instance, name, ...)
   -- Borrow signal format from GObject.ObjectClass.notify.
   local info = gi.GObject.ObjectClass.fields.notify.typeinfo.interface
   if select('#', ...) > 0 then
      -- Assignment means 'connect signal for all properties'.
      core.object.connect(instance, info.name, info, get_notifier(...))
   else
      -- Reading yields table with signal operations.
      local pad = {}
      function pad:connect(target, property)
	 return core.object.connect(instance, info.name, info,
				    get_notifier(target),
				    property:gsub('_', '%-'))
      end

      -- Add metatable allowing connection on specified detail.
      -- Detail is always specified as a property name for this signal.
      local padmeta = {}
      function padmeta:__newindex(property, target)
	 core.object.connect(instance, info.name, info,
			     get_notifier(target), property:gsub('_', '%-'))
      end
      return setmetatable(pad, padmeta)
   end
end

-- Closure modifications.  Closure does not need any methods nor
-- fields, but it must have constructor creating it from any kind of
-- Lua callable.
local closure = repo.GObject.Closure
local closure_info = gi.GObject.Closure
closure._methods = nil
closure._fields = nil
local closure_mt = { __index = getmetatable(closure).__index,
		     _access = getmetatable(closure)._access }
function closure_mt:__call(arg)
   return core.record.new(closure_info, arg)
end
setmetatable(closure, closure_mt)

-- Implicit conversion constructor, allows using Lua function directly
-- at the places where GClosure is expected.
function closure:_construct(arg)
   return core.record.new(closure_info, arg)
end

-- Value is constructible from any kind of source Lua value, and the
-- type of the value can be hinted by type name.
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
   error(("unclear type for GObject.Value from argument `%s'"):format(
	    tostring(source)))
end

-- Implement constructor, taking any source value and optionally type
-- of the source.
local value_mt = {}
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

-- Access to module proxies the whole repo, for convenience.
local lgi_mt = {}
function lgi_mt:__index(name)
   return repo[name]
end
return setmetatable(lgi, lgi_mt)
