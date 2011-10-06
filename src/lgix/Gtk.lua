------------------------------------------------------------------------------
--
--  LGI Gtk3 override module.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs, unpack = select, type, pairs, unpack
local lgi = require 'lgi'
local core = require 'lgi._core'
local gi = core.gi
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local GObject = lgi.GObject

-- Gtk.Allocation is just an alias to Gdk.Rectangle.
Gtk.Allocation = Gdk.Rectangle

-------------------------------- Gtk.Widget overrides.
Gtk.Widget._attribute = {}

-- gtk_widget_intersect is missing an (out caller-allocates) annotation
if core.gi.Gtk.Widget.methods.intersect.args[2].direction == 'in' then
   local real_intersect = Gtk.Widget.intersect
   function Gtk.Widget._method:intersect(area)
      local intersection = Gdk.Rectangle()
      local notempty = real_intersect(self, area, intersection)
      return notempty and intersection or nil
   end
end

-- Accessing style properties is preferrably done by accessing 'style'
-- property.  In case that caller wants deprecated 'style' property, it
-- must be accessed by '_property_style' name.
Gtk.Widget._attribute.style = {}
local style_mt = {}
function style_mt:__index(name)
   name = name:gsub('_', '%-')
   local pspec = self._widget.class:find_style_property(name)
   local value = GObject.Value(pspec.value_type)
   self._widget:style_get_property(name, value)
   return value.value
end
function Gtk.Widget._attribute.style:get()
   return setmetatable({ _widget = self }, style_mt)
end

-------------------------------- Gtk.Container overrides.
Gtk.Container._attribute = {}

-- Accessing child properties is preferrably done by accessing
-- 'children' property.
Gtk.Container._attribute.children = {}
local child_mt = {}
function child_mt:__index(name)
   name = name:gsub('_', '%-')
   local pspec = self._container.class:find_child_property(name)
   local value = GObject.Value(pspec.value_type)
   self._container:child_get_property(self._child, name, value)
   return value.value
end
function child_mt:__newindex(name, val)
   name = name:gsub('_', '%-')
   local pspec = self._container.class:find_child_property(name)
   local value = GObject.Value(pspec.value_type, val)
   self._container:child_set_property(self._child, name, value)
end
local children_mt = {}
function children_mt:__index(child)
   return setmetatable({ _container = self._container, _child = child }, child_mt)
end
function Gtk.Container._attribute.children:get()
   return setmetatable({ _container = self }, children_mt)
end

local container_add = Gtk.Container.add
function Gtk.Container._method:add(widget, props)
   container_add(self, widget)
   local child_props = self.children[widget]
   for name, value in pairs(props or {}) do child_props[name] = value end
end

-- Initialize GTK.
Gtk.init()
