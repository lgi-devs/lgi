------------------------------------------------------------------------------
--
--  LGI Lua-side core.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
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
local core = require 'lgi.core'

-- Initialize GI wrapper from the core.
local gi = core.gi
assert(gi.require ('GLib', '2.0'))
assert(gi.require ('GObject', '2.0'))

-- Create lgi table, containing the module.
local lgi = { _NAME = 'lgi', _VERSION = require 'lgi.version' }

-- Add simple flag-checking function, avoid compatibility hassle with
-- importing bitlib just because of this simple operation.
function core.has_bit(value, flag)
   return value % (2 * flag) >= flag
end

-- Forward 'yield' functionality into external interface.
lgi.yield = core.yield

-- If global package 'bytes' does not exist (i.e. not provided
-- externally), use our internal (although incomplete) implementation.
local ok, bytes = pcall(require, 'bytes')
if not ok or not bytes then
   package.loaded.bytes = core.bytes
end

-- Prepare logging support.  'log' is module-exported table, containing all
-- functionality related to logging wrapped around GLib g_log facility.
lgi.log = require 'lgi.log'

-- For the rest of bootstrap, prepare logging to lgi domain.
local log = lgi.log.domain('lgi')
log.message('gobject-introspection binding for Lua, ' .. lgi._VERSION)

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

local component = require 'lgi.component'

-- Implementation of record_mt, which is inherited from component
-- and provides customizations for structures and unions.
local record_mt = component.mt:clone { '_method', '_field' }

function record_mt:_element(instance, symbol)
   -- First of all, try normal inherited functionality.
   local element, category = component.mt._element(self, instance, symbol)
   if element then return element, category end

   -- If the record has parent struct, try it there.
   local parent = rawget(self, '_parent')
   if parent then
      element, category = parent:_element(instance, symbol)
      if element then
	 -- If category shows that returned element is already from
	 -- inherited, leave it so, otherwise wrap returned element
	 -- into _inherited category.
	 if category ~= '_inherited' then
	    element = { element = element, category = category,
			symbol = symbol, type = parent }
	    category = '_inherited'
	 end
	 return element, category
      end
   end
end

-- Add accessor for handling fields.
function record_mt:_access_field(instance, element, ...)
   assert(gi.isinfo(element) and element.is_field)
   -- Check the type of the field.
   local ii = element.typeinfo.interface
   if ii and (ii.type == 'struct' or ii.type == 'union') then
      -- Nested structure, handle assignment to it specially.  Get
      -- access to underlying nested structure.
      local subrecord = core.record.field(instance, element)

      -- Reading it is simple, we are done.
      if select('#', ...) == 0 then return subrecord end

      -- Writing means assigning all fields from the source table.
      for name, value in pairs(...) do subrecord[name] = value end
   else
      -- In other cases, just access the instance using given info.
      return core.record.field(instance, element, ...)
   end
end

-- Add accessor for accessing inherited elements.
function record_mt:_access_inherited(instance, element, ...)
   -- Cast instance to inherited type.
   instance = core.record.cast(instance, element.type)

   -- Forward to normal _access_element implementation.
   return self:_access_element(instance, element.category, element.symbol,
			       element.element, ...)
end

-- Create structure instance and initialize it with given fields.
function record_mt:_new(fields)
   -- Find baseinfo of requested record.
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

   -- Create the structure instance.
   local struct = core.record.new(info)

   -- Set values of fields.
   for name, value in pairs(fields or {}) do struct[name] = value end
   return struct
end

-- Implementation of interface_mt.
local interface_mt = component.mt:clone {
   '_virtual', '_property', '_signal', '_method', '_constant'
}

-- Checks whether given argument is type of this class.
function interface_mt:is_type_of(instance)
   if type(instance) == 'userdata' then
      local instance_type = core.object.query(instance, 'repo')
      while instance_type do
	 if instance_type == self
	    or instance_type._implements[self._name] == self then
	    return true
	 end
	 instance_type = rawget(instance_type, '_parent')
      end
   end
   return false
end

-- Implementation of class_mt, inherited from component and
-- providing basic class functionality.  Note that signals and
-- properties are implemented later on GObject descendants only.
local class_mt = interface_mt:clone {
   '_virtual', '_property', '_signal', '_method', '_constant', '_field'
}

-- Resolver for classes, recursively resolves also all parents and
-- implemented interfaces.
function class_mt:_resolve(recursive)
   -- Resolve itself using inherited implementation.
   component.mt._resolve(self)

   -- Go to parent and implemented interfaces and resolve them too.
   if recursive then
      for _, iface in pairs(self._implements or {}) do
	 iface:_resolve(recursive)
      end
      if self._parent then
	 self._parent:_resolve(recursive)
      end
   end
   return self
end

-- _element implementation for objects, checks parent and implemented
-- interfaces if element cannot be found in current typetable.
function class_mt:_element(instance, symbol)
   -- Check default implementation.
   local element, category = component.mt._element(self, instance, symbol)
   if element then return element, category end

   -- Check parent and all implemented interfaces.
   local parent = rawget(self, '_parent')
   if parent then
      element, category = parent:_element(instance, symbol)
      if element then return element, category end
   end
   local implements = rawget(self, '_implements') or {}
   for _, implemented in pairs(implements or {}) do
      element, category = implemented:_element(instance, symbol)
      if element then return element, category end
   end
end

-- Implementation of field accessor.  Note that compound fields are
-- not supported in classes (because they are not seen in the wild and
-- I'm lazy).
function class_mt:_access_field(instance, field, ...)
   return core.object.field(instance, field, ...)
end

-- Implementation of virtual method accessor.  Virtuals are
-- implemented by accessing callback pointer in the class struct of
-- the class.  Note that currently we support only reading of them,
-- writing would mean overriding, which is not supported yet.
function class_mt:_access_virtual(instance, vfunc, ...)
   if select('#', ...) > 0 then
      error(("%s: cannot override virtual `%s' "):format(
	       self._name, vfunc.name), 5)
   end
   -- Get typestruct of this class.
   local typestruct = core.object.query(instance, 'class',
					vfunc.container.gtype)

   -- Resolve the field of the typestruct with the virtual name.  This
   -- returns callback to the virtual, which can be directly called.
   return core.record.field(typestruct, self._class[vfunc.name])
end

-- Object constructor, does not accept any arguments.  Overriden later
-- for GObject which accepts properties table to initialize object
-- with.
local object_new = gi.require('GObject').Object.methods.new
if object_new then
   object_new = core.callable.new(object_new)
else
   -- Unfortunately, older GI (<1.30) does not export g_object_newv()
   -- in the typelib, so we have to workaround here with manually
   -- implemented C version.
   object_new = core.object.new
end
function class_mt:_new()
   -- Create the object.
   return object_new(self._gtype, {})
end

-- Table containing loaders for various GI types, indexed by
-- gi.InfoType constants.
local typeloader = {}

typeloader['function'] =
   function(namespace, info)
      return core.callable.new(info), '_function'
   end

function typeloader.constant(namespace, info)
   return core.constant(info), '_constant'
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

-- Enum reverse mapping, value->name.
local enum_mt = {}
function enum_mt:__index(value)
   for name, val in pairs(self) do
      if val == value then return name end
   end
end

function typeloader.enum(namespace, info)
   return load_enum(info, enum_mt), '_enum'
end

-- Resolving arbitrary number to the table containing symbolic names
-- of contained bits.
local bitflags_mt = {}
function bitflags_mt:__index(value)
   if type(value) ~= 'number' then return end
   local t = {}
   for name, flag in pairs(self) do
      if type(flag) == 'number' and core.has_bit(value, flag) then
	 t[name] = flag
      end
   end
   return t
end

function typeloader.flags(namespace, info)
   return load_enum(info, bitflags_mt), '_enum'
end

local function load_signal_name(name)
   name = name:match('^on_(.+)$')
   return name and name:gsub('_', '%-')
end

local function load_signal_name_reverse(name)
   return 'on_' .. name:gsub('%-', '_')
end

local function load_vfunc_name(name)
   return name:match('^virtual_(.+)$')
end

local function load_vfunc_name_reverse(name)
   return 'virtual_' .. name
end

local function load_method(mi)
   local flags = mi.flags
   if not flags.is_getter and not flags.is_setter then
      return core.callable.new(mi)
   end
end

-- Loads structure information into table representing the structure
local function load_record(info)
   local record = create_component(info, record_mt)
   record._method = component.get_category(info.methods, core.callable.new)
   record._field = component.get_category(info.fields)

   -- Check, whether global namespace contains 'constructor' method,
   -- i.e. method which has the same name as our record type (except
   -- that type is in CamelCase, while method is
   -- under_score_delimited).  If not found, check for 'new' method.
   local func = info.name:gsub('([%l%d])([%u])', '%1_%2'):lower()
   local ctor = gi[info.namespace][func]
   if not ctor then ctor = info.methods.new end

   -- Check, whether ctor is valid.  In order to be valid, it must
   -- return instance of this record.
   if (ctor and ctor.return_type.tag =='interface'
       and ctor.return_type.interface == info) then
      ctor = core.callable.new(ctor)
      record._new = function(typetable, ...) return ctor(...) end
   end
   return record
end

function typeloader.struct(namespace, info)
   -- Avoid exposing internal structs created for object implementations.
   if not info.is_gtype_struct then
      return load_record(info), '_struct'
   end
end

function typeloader.union(namespace, info)
   return load_record(info), '_union'
end

local function load_properties(info)
   return component.get_category(
      info.properties, nil,
      function(name) return string.gsub(name, '_', '%-') end,
      function(name) return string.gsub(name, '%-', '_') end)
end

local function find_constructor(info)
   local name = info.name:gsub('([%d%l])(%u)', '%1_%2'):lower()
   local ctor = gi[info.namespace][name]

   -- Check that return value conforms to info type.
   if ctor then
      local ret = ctor.return_type.interface
      for walk in function(_, c) return c.parent end, nil, info do
	 if ret and walk == ret then
	    ctor = core.callable.new(ctor)
	    return function(self, ...) return ctor(...) end
	 end
      end
   end
end

function typeloader.interface(namespace, info)
   -- Load all components of the interface.
   local interface = component.create(info, interface_mt)
   interface._property = load_properties(info)
   interface._method = component.get_category(info.methods, load_method)
   interface._signal = component.get_category(
      info.signals, nil, load_signal_name, load_signal_name_reverse)
   interface._constant = component.get_category(info.constants, core.constant)
   local type_struct = info.type_struct
   if type_struct then
      interface._virtual = component.get_category(
	 info.vfuncs, nil, load_vfunc_name, load_vfunc_name_reverse)
      interface._class = load_record(type_struct)
   end
   interface._new = find_constructor(info)
   return interface, '_interface'
end

function typeloader.object(namespace, info)
   -- Find parent record, if available.
   local parent_info, parent = info.parent
   if parent_info then
      local ns, name = parent_info.namespace, parent_info.name
      if ns ~= namespace._name or name ~= info.name then
	 parent = repo[ns][name]
      end
   end

   -- Create class instance, copy mt from parent, if parent exists,
   -- otherwise defaults to class_mt.
   local class = component.create(
      info, parent and getmetatable(parent) or class_mt)
   class._parent = parent
   class._property = load_properties(info)
   class._method = component.get_category(info.methods, load_method)
   class._signal = component.get_category(
      info.signals, nil, load_signal_name, load_signal_name_reverse)
   class._constant = component.get_category(info.constants, core.constant)
   class._field = component.get_category(info.fields)
   local type_struct = info.type_struct
   if type_struct then
      class._virtual = component.get_category(
	 info.vfuncs, nil, load_vfunc_name, load_vfunc_name_reverse)
      class._class = load_record(type_struct)
      if parent then class._class._parent = parent._class end
   end

   -- Populate inheritation information (_implements and _parent fields).
   local interfaces, implements = info.interfaces, {}
   for i = 1, #interfaces do
      local iface = interfaces[i]
      implements[iface.fullname] = repo[iface.namespace][iface.name]
   end
   class._implements = implements
   class._new = find_constructor(info)
   return class, '_class'
end

-- Repo namespace metatable.
local namespace_mt = {
   _categories = { '_class', '_interface', '_struct', '_union', '_enum',
		   '_function', '_constant', } }

-- Gets symbol of the specified namespace, if not present yet, tries to load it
-- on-demand.
function namespace_mt:__index(symbol)
   -- Check whether symbol is present in the metatable.
   local val = namespace_mt[symbol]
   if val then return val end

   -- Check, whether there is some precondition in the lazy-loading table.
   local preconditions = rawget(self, '_precondition')
   local precondition = preconditions and preconditions[symbol]
   if precondition then
      local package = preconditions[symbol]
      if not preconditions[package] then
	 preconditions[package] = true
	 require('lgi.override.' .. package)
	 preconditions[package] = nil
      end
      preconditions[symbol] = nil
      if not next(preconditions) then self._precondition = nil end
   end

   -- Check, whether symbol is already loaded.
   val = component.mt._element(self, nil, symbol, namespace_mt._categories)
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
      if val then
	 local cat = rawget(self, category)
	 if not cat then
	    cat = {}
	    self[category] = cat
	 end
	 -- Store symbol into the repo, but only if it is not already
	 -- there.  It could by added to repo as byproduct of loading
	 -- other symbol.
	 if not cat[symbol] then cat[symbol] = val end
      end
   end
   return val
end

-- Resolves everything in the namespace by iterating through it.
function namespace_mt:_resolve(recurse)
   -- Iterate through all items in the namespace and dereference them,
   -- which causes them to be loaded in and cached inside the namespace
   -- table.
   local gi_ns = gi[self._name]
   for i = 1, #gi_ns do
      local ok, component = pcall(function() return self[gi_ns[i].name] end)
      if ok and recurse and type(component) == 'table' then
	 local resolve = component._resolve
	 if resolve then resolve(component, recurse) end
      end
   end
   return self
end

-- Makes sure that the namespace (optionally with requested version)
-- is properly loaded.
function lgi.require(name, version)
   -- Load the namespace info for GIRepository.  This also verifies
   -- whether requested version can be loaded.
   local ns_info = assert(gi.require(name, version))

   -- If the repository table does not exist yet, create it.
   local ns = rawget(repo, name)
   if not ns then
      ns = setmetatable({ _name = name, _version = ns_info.version,
			  _dependencies = ns_info.dependencies },
			namespace_mt)
      repo[name] = ns

      -- Make sure that all dependent namespaces are also loaded.
      for name, version in pairs(ns._dependencies or {}) do
	 lgi.require(name, version)
      end

      -- Try to load override, if it is present.
      local override_name = 'lgi.override.' .. ns._name
      local ok, msg = pcall(require, override_name)
      if not ok then
	 -- Try parsing message; if it is something different than
	 -- "module xxx not found", then attempt to load again and let
	 -- the exception fly out.
	 if not msg:find("module '" .. override_name .. "' not found:",
			 1, true) then
	    package.loaded[override_name] = nil
	    require(override_name)
	 end
      end
   end
   return ns
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(_, name) return lgi.require(name) end })

repo.GObject._precondition = {}
for _, name in pairs { 'Type', 'Value', 'Closure', 'Object' } do
   repo.GObject._precondition[name] = 'GObject-' .. name
end
repo.GObject._precondition.InitiallyUnowned = 'GObject-Object'

-- Create lazy-loading components for variant stuff.
repo.GLib._precondition = {}
for _, name in pairs { 'Variant', 'VariantType', 'VariantBuilder' } do
   repo.GLib._precondition[name] = 'GLib-Variant'
end

-- Access to module proxies the whole repo, for convenience.
return setmetatable(lgi, { __index = repo })
