------------------------------------------------------------------------------
--
--  lgi GObject.Object handling.
--
--  Copyright (c) 2010-2014 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local pairs, select, setmetatable, error, type
   = pairs, select, setmetatable, error, type

local core = require 'lgi.core'
local gi = core.gi
local repo = core.repo
local ffi = require 'lgi.ffi'
local ti = ffi.types

local Value = repo.GObject.Value
local Type = repo.GObject.Type
local Closure = repo.GObject.Closure

local TypeClass = repo.GObject.TypeClass
local Object = repo.GObject.Object

-- Add overrides for GObject.TypeClass
TypeClass._free = gi.GObject.resolve.g_type_class_unref
local type_class_ref = core.callable.new {
   addr = gi.GObject.resolve.g_type_class_ref,
   ret = ti.ptr, ti.GType
}
function TypeClass:_new()
   local ptr = type_class_ref(self._gtype)
   return core.record.new(self, ptr, true)
end

-- Object constructor, 'param' contains table _with properties/signals
-- to initialize.
local parameter_repo = repo.GObject.Parameter
-- Before GLib 2.54, g_object_newv() was annotated with [rename-to g_object_new].
-- Starting from GLib 2.54, g_object_new_with_properties() has this annotation.
-- We always want g_object_newv().
local object_new = gi.GObject.Object.methods.newv or gi.GObject.Object.methods.new
if object_new then
   object_new = core.callable.new(object_new)
else
   -- Unfortunately, older GI (<1.30) does not export g_object_newv()
   -- in the typelib, so we have to workaround here with manually
   -- implemented C version.
   object_new = core.object.new
end

-- Generic construction method.
function Object:_construct(gtype, param, owns)
   local object
   if type(param) == 'userdata' then
      -- Wrap existing GObject instance in the lgi proxy.
      object = core.object.new(param, owns)
      gtype = object._gtype
   end

   -- Check that gtype fits.
   if not Type.is_a(gtype, self._gtype) then
      error(("`%s' is not subtype of `%s'"):format(
	       Type.name(gtype), self._name), 3)
   end

   -- In 'wrap' mode, just return the created object.
   if object then return object end

   -- Process 'args' table, separate properties from other fields.
   local parameters, others, safe = {}, {}, {}
   for name, arg in pairs(param or {}) do
      if type(name) == 'string' then
	 local argtype = self[name]
	 if gi.isinfo(argtype) and argtype.is_property then
	    local parameter = core.record.new(parameter_repo)
	    name = argtype.name
	    local value = parameter.value

	    -- Store the name string in some safe Lua place ('safe'
	    -- table), because param is GParameter, which contains
	    -- only non-owning pointer to the string, and it could be
	    -- Lua-GC'ed while still referenced by GParameter
	    -- instance.
	    safe[#safe + 1] = name
	    safe[#safe + 1] = value

	    parameter.name = name
	    local gtype = Type.from_typeinfo(argtype.typeinfo)
	    Value.init(value, gtype)
	    local marshaller = Value.find_marshaller(gtype, argtype.typeinfo)
	    marshaller(value, nil, arg)
	    parameters[#parameters + 1] = parameter
	 else
	    others[name] = arg
	 end
      end
   end

   -- Create the object.
   object = object_new(gtype, parameters)

   -- Perform initialization on interfaces.
   if next(self._implements) then
      local inited
      for _, initname in ipairs { '_init1', '_init2' } do
	 for _, interface in pairs(self._implements) do
	    local init = interface[initname]
	    if init then
	       local ok, err = init(object)
	       if not ok then return nil, err end
	       if ok == '_initskip' then
		  -- This initializer does not apply, continue looking
		  -- for others.
	       else
		  inited = true
		  break;
	       end
	    end
	 end
	 if inited then break end
      end
   end

   -- Attach arguments previously filtered out from creation.
   for name, value in pairs(others) do
      if type(name) == 'string' then object[name] = value end
   end

   -- In case that type has _container_add() method, use it to process
   -- array part of the args.
   local add = self._container_add
   if add and param then
      for i = 1, #param do add(object, param[i]) end
   end
   return object
end

function Object:_new(...)
   -- Invoke object's construct method which does the work.
   return self:_construct(self._gtype, ...)
end

-- Override normal 'new' constructor, to allow creating objects with
-- specified GType.
function Object.new(gtype, params, owns)
   -- Find proper repo instance for gtype.
   local gtype_walker = gtype
   while true do
      local self = core.repotype(gtype_walker)
      if self then
	 -- We have repo instance, use it to construct the object.
	 return self:_construct(gtype, params, owns)
      end
      gtype_walker = Type.parent(gtype_walker)
      if not gtype_walker then
	 error(("`%s': cannot create object, type not found"):format(gtype), 2)
      end
   end
end

-- Prepare callbacks for get_property and set_property
local get_property_guard, get_property_addr = core.marshal.callback(
   gi.GObject.ObjectClass.fields.get_property.typeinfo.interface,
   function(self, prop_id, value, pspec)
      local name = pspec.name:gsub('%-', '_')
      local prop_get = core.object.query(self, 'repo')._property_get[name]
      if prop_get then
	 value.value = prop_get(self)
      else
	 value.value = self.priv[name]
      end
end)

local set_property_guard, set_property_addr = core.marshal.callback(
   gi.GObject.ObjectClass.fields.get_property.typeinfo.interface,
   function(self, prop_id, value, pspec)
      local name = pspec.name:gsub('%-', '_')
      local prop_set = core.object.query(self, 'repo')._property_set[name]
      if prop_set then
	 prop_set(self, value.value)
      else
	 self.priv[name] = value.value
      end
end)

if not core.guards then core.guards = {} end
core.guards.get_property = get_property_guard
core.guards.set_property = set_property_guard

-- _class_init method on the Object will install all properties
-- accumulated in _property table.  It will be called automatically on
-- derived classes during class initialization routine.
function Object:_class_init(class)
   if next(self._property) then
      -- First install get/set_property overrides, unless already present.
      if not self._override.get_property then
	 class.get_property = get_property_addr
      end
      if not self._override.set_property then
	 class.set_property = set_property_addr
      end

      -- Install properties.
      local prop_id = 0
      for name, pspec in pairs(self._property) do
	 prop_id = prop_id + 1
	 class:install_property(prop_id, pspec)
      end
   end
end

-- Initially unowned creation is similar to normal GObject creation,
-- but we have to ref_sink newly created object.
local InitiallyUnowned = repo.GObject.InitiallyUnowned
function InitiallyUnowned:_construct(...)
   local object = Object._construct(self, ...)
   return Object.ref_sink(object)
end

-- Reading 'class' yields real instance of the object class.
Object._attribute = { _type = {} }
function Object._attribute._type:get()
   return core.object.query(self, 'repo')
end

-- Custom _element implementation, checks dynamically inherited
-- interfaces and dynamic properties.
local inherited_element = Object._element
function Object:_element(object, name)
   local element, category = inherited_element(self, object, name)
   if element then return element, category end

   -- Everything else works only if we have object instance.
   if not object then return nil end

   -- List all interfaces implemented by this object and try whether
   -- they can handle specified _element request.
   local interfaces = Type.interfaces(object._gtype)
   for i = 1, #interfaces do
      local info = gi[core.gtype(interfaces[i])]
      local iface = info and repo[info.namespace][info.name]
      if iface then element, category = iface:_element(object, name, self) end
      if element then return element, category end
   end

   -- Element not found in the repo (typelib), try whether dynamic
   -- property of the specified name exists.
   local property = Object._class.find_property(
      object._class, name:gsub('_', '-'))
   if property then return property, '_property' end
end

-- Sets/gets property using specified marshaller attributes.
local function marshal_property(obj, name, flags, gtype, marshaller, ...)
   -- Check access rights of the property.
   local mode = select('#', ...) > 0 and 'WRITABLE' or 'READABLE'
   if not flags[mode] then
      error(("%s: `%s' not %s"):format(core.object.query(obj, 'repo')._name,
				       name, core.downcase(mode)))
   end
   local value = Value(gtype)
   if mode == 'WRITABLE' then
      marshaller(value, nil, ...)
      Object.set_property(obj, name, value)
   else
      Object.get_property(obj, name, value)
      return marshaller(value)
   end
end

-- Property accessor.
function Object:_access_property(object, prop, ...)
   if gi.isinfo(prop) then
      -- GI-based property
      local typeinfo = prop.typeinfo
      local gtype = Type.from_typeinfo(typeinfo)
      local marshaller = Value.find_marshaller(gtype, typeinfo,
					       prop.transfer)
      return marshal_property(object, prop.name,
			      repo.GObject.ParamFlags[prop.flags],
			      gtype, marshaller, ...)
   else
      -- pspec-based property
      return marshal_property(object, prop.name, prop.flags, prop.value_type,
			      Value.find_marshaller(prop.value_type), ...)
   end
end

local quark_from_string = repo.GLib.quark_from_string
local signal_lookup = repo.GObject.signal_lookup
local signal_connect_closure_by_id = repo.GObject.signal_connect_closure_by_id
local signal_emitv = repo.GObject.signal_emitv
-- Connects signal to specified object instance.
local function connect_signal(obj, gtype, name, closure, detail, after)
   return signal_connect_closure_by_id(
      obj, signal_lookup(name, gtype),
      detail and quark_from_string(detail) or 0,
      closure, after or false)
end
-- Emits signal on specified object instance.
local function emit_signal(obj, gtype, info, detail, ...)
   -- Compile callable info.
   local call_info = Closure.CallInfo.new(info)

   -- Marshal input arguments.
   local retval, params, marshalling_params = call_info:pre_call(obj, ...)

   -- Invoke the signal.
   signal_emitv(params, signal_lookup(info.name, gtype),
		detail and quark_from_string(detail) or 0, retval)

   -- Unmarshal results.
   return call_info:post_call(params, retval, marshalling_params)
end

-- Signal accessor.
function Object:_access_signal(object, info, ...)
   local gtype = self._gtype
   if select('#', ...) > 0 then
      -- Assignment means 'connect signal without detail'.
      connect_signal(object, gtype, info.name, Closure((...), info))
   else
      -- Reading yields table with signal operations.
      local mt = {}
      local pad = setmetatable({}, mt)
      function pad:connect(target, detail, after)
	 return connect_signal(object, gtype, info.name,
			       Closure(target, info), detail, after)
      end
      function pad:emit(...)
	 return emit_signal(object, gtype, info, nil, ...)
      end
      function mt:__call(_, ...)
	 return emit_signal(object, gtype, info, nil, ...)
      end

      -- If signal supports details, add metatable implementing
      -- __newindex for connecting in the 'on_signal['detail'] =
      -- handler' form.
      if not info.is_signal or info.flags.detailed then
	 function pad:emit(detail, ...)
	    return emit_signal(object, gtype, info, detail, ...)
	 end
	 function mt:__newindex(detail, target)
	    connect_signal(object, gtype, info.name, Closure(target, info),
			   detail)
	 end
      end

      -- Return created signal pad.
      return pad
   end
end

-- GOI<1.30 does not export 'Object.on_notify' signal from the
-- typelib.  Work-around this problem by implementing custom on_notify
-- attribute.
if not gi.GObject.Object.signals.notify then
   local notify_info = gi.GObject.ObjectClass.fields.notify.typeinfo.interface
   function Object._attribute.on_notify(object, ...)
      local repotable = core.object.query(object, 'repo')
      return Object._access_signal(repotable, object, notify_info, ...)
   end
end

-- Bind property implementation.  For some strange reason, GoI<1.30
-- exports it only on GInitiallyUnowned and not on GObject.  Oh
-- well...
for _, name in pairs { 'bind_property', 'bind_property_full' } do
   if not Object[name] then
      Object._method[name] = InitiallyUnowned[name]
   end
end
