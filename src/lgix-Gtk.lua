------------------------------------------------------------------------------
--
--  LGI Gtk3 override module.
--
--  Copyright (c) 2010 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs = select, type, pairs
local lgi = require 'lgi'
local core = require 'lgi._core'
local Gtk = lgi.Gtk
local GObject = lgi.GObject

-------------------- Gtk.Container
function Gtk.Container:child_set(child, properties)
   -- Assign a bunch (table) of child properties at once.
   Gtk.Widget.freeze_child_notify(child)
   for name, value in pairs(properties) do
      -- Ignore non-string names.
      if type(name) == 'string' then
	 Gtk.Container.child_set_property(self, child, 
					  name:gsub('_', '%-'), value);
      end
   end
   Gtk.Widget.thaw_child_notify(child)
end

function Gtk.Container:add_with_properties(child, properties)
   Gtk.Container.add(self, child)
   Gtk.Container.child_set(self, child, properties)
end

local container_prop_child_mt = {}
function container_prop_child_mt:__index(name)
   if name == 'on_notify' then
      -- TODO: Handling of child_notify signal.
   else
      local class = core.object.query(instance, 'class', Gtk.Container)
      local pspec = class:find_child_property(name:gsub('_', '%-'))
      local value = GObject.Value(pspec.value_type)
      class.get_child_property(self._container, self._child, pspec.param_id,
			       value, pspec)
      return value.data
   end
end

function container_prop_child_mt:__newindex(name, newval)
   if name == 'on_notify' then
      -- TODO: Handling of child_notify signal.
   else
      -- Set specific child property.
      local class = core.object.query(instance, 'class', Gtk.Container)
      local pspec = class:find_child_property(name:gsub('_', '%-'))
      local value = GObject.Value(pspec.value_type, newval)
      class.set_child_property(self._container, self._child, pspec.param_id,
			       value, pspec)
   end
end

local container_prop_children_mt = {}
function container_prop_children_mt:__index(child)
   -- Return table which retrieves child properties on-demand.
   return setmetatable({ _container = self._container, _child = child },
		       container_prop_child_mt)
end

function container_prop_children_mt:__newindex(child, properties)
   -- Proxies to child_set().
   Gtk.Container.child_set(self._container, child, properties)
end

local function container_add_child(container, child_specs)
   if type(child_specs) == 'table' then
      Gtk.Container.add_with_properties(container, child_specs[1], child_specs)
   else
      Gtk.Container.add(container, child_specs)
   end
end

local container_access_element = Gtk.Container._access_element
function Gtk.Container:_access_element(container, name, element, ...)
   if name == 'children' then
      if select('#', ...) == 0 then
	 -- Reading yields the table of all children.
	 return self.get_children(container)
      else
	 -- Writing adds new children, optionally with specified child
	 -- properties.
	 local arg = ...
	 for i = 1, #arg do container_add_child(container, arg[i]) end
	 return
      end
   elseif name == 'child' then
      if select('#', ...) == 0 then
	 -- Reading generates table used for getting/setting child
	 -- properties on specified child.
	 return setmetatable({ _container = container },
			     container_prop_children_mt)
      else
	 -- Writing adds new child.
	 container_add_child(container, ...)
      end
   end

   return container_access_element(self, container, name, element, ...)
end

-------------------- Gtk.CellLayout

local celllayout_access_element = Gtk.CellLayout._access_element
function Gtk.CellLayout:_access_element(layout, name, element, ...)
   if name == 'attributes' then
      assert(select('#', ...) == 1, "Gtk.CellLayout: '" .. name .. 
	  "' is not readable")
      self.clear(column)
      for attr, column_number in pairs(...) do
	 if type(attr) == 'userdata' then attr = attr.name end
	 self.add_attribute(column, core.object.env(column).cell, attr,
			    column_number)
      end
   end
   return celllaout_access_element(layout, name, element, ...)
end

-------------------- Gtk.Builder

local builder_objects_mt = {}
function builder_objects_mt:__index(name)
   if type(name) == 'string' then
      local object = self._builder:get_object(name)
      self[name] = object
      return object
   end
end
local builder_access_element = Gtk.Builder._access_element
function Gtk.Builder:_access_element(builder, name, element, ...)
   -- Detect 'objects' property request.
   if name == 'objects' then
      assert(select('#', ...) == 0, "Gtk.Builder: 'objects' is not writable")

      -- Get all objects and add metatable for resolving by name.
      local objects = builder:get_objects()
      objects._builder = builder
      return setmetatable(objects, builder_objects_mt)
   elseif name == 'file' or name == 'string' then
      -- Load all specified files.
      assert(select('#', ...) == 1, "Gtk.Builder: '" .. name ..
	  "' is not readable")
      local func = (name == 'file' and self.add_from_file
		    or self.add_from_string)
      local arg = ...
      if type(arg) == table then
	 for i = 1, #arg do func(builder, arg[i]) end
      else
	 func(builder, arg)
      end
      return
   end

   -- Forward to original method.
   return builder_access_element(self, builder, name, element, ...)
end

-- Initialize GTK.
Gtk.init()
