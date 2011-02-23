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
   local class = core.object.query(self, 'class', Gtk.Container)
   Gtk.Widget.freeze_child_notify(child)
   for name, value in pairs(properties) do
      -- Ignore non-string names.
      if type(name) == 'string' then
	 local pspec = class:find_child_property(name:gsub('_', '%-'))
	 Gtk.Container.child_set_property(
	    self, child, name:gsub('_', '%-'),
	    GObject.Value(pspec.value_type, value));
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

Gtk.Container._custom = { children = {}, child = {} }
-- Reading yields the table of all children.
Gtk.Container._custom.children.get = Gtk.Container.get_children

-- Writing adds new children, optionally with specified child
-- properties.
function Gtk.Container._custom.children.set(container, children)
   for i = 1, #children do container_add_child(container, children[i]) end
end

-- Reading generates table used for getting/setting child properties
-- on specified child.
function Gtk.Container._custom.child.get(container)
   return setmetatable({ _container = container }, container_prop_children_mt)
end

-- Writing adds new child.
Gtk.Container._custom.child.set = container_add_child

-------------------- Gtk.TreeModel

local treemodel_row_mt = {}
function treemodel_row_mt:__index(column)
   return Gtk.TreeModel.get_value(self._model, self._iter, column).data
end

local treemodel_values_mt = {}
function treemodel_values_mt:__index(iter)
   if core.record.query(iter, 'repo') == Gtk.TreePath then
      local ok, real_iter = Gtk.TreeModel.get_iter(self._model, iter)
      if not ok then return nil end
      iter = real_iter
   elseif type(iter) == 'string' then
      local ok, real_iter = Gtk.TreeModel.get_iter_from_string(
	 self._model, iter)
      if not ok then return nil end
      iter = real_iter
   end
   return setmetatable({ _model = self._model, _iter = iter }, self._row_mt)
end

Gtk.TreeModel._custom = { values = {} }
function Gtk.TreeModel._custom.values.get(model)
   return setmetatable({ _model = model, _row_mt = treemodel_row_mt },
		       treemodel_values_mt)
end

-------------------- Gtk.ListStore

local liststore_row_mt = { __index = treemodel_row_mt.__index }
function liststore_row_mt:__newindex(column, val)
   local gtype = Gtk.TreeModel.get_column_type(self._model, column)
   Gtk.ListStore.set_value(self._model, self._iter, column,
			   GObject.Value(gtype, val))
end

local liststore_values_mt = { __index = treemodel_values_mt.__index }
function liststore_values_mt:__newindex(iter, vals)
   local columns, values = {}, {}
   for i = 1, #vals do
      local gtype = Gtk.TreeModel.get_column_type(self._model, i - 1)
      values[i] = GObject.Value(gtype, vals[i])
      columns[i] = i - 1
   end
   Gtk.ListStore.set(self._model, iter, columns, values)
end

Gtk.ListStore._custom = { values = {} }
function Gtk.ListStore._custom.values.get(model)
   return setmetatable({ _model = model, _row_mt = liststore_row_mt },
		       liststore_values_mt)
end

-------------------- Gtk.TreeViewColumn

function Gtk.TreeViewColumn:set_attributes(cell, attrs)
   for colnum, attr in pairs(attrs) do
      if type(attr) ~= 'string' then attr = attr.name end
      Gtk.TreeViewColumn.add_attribute(self, cell, attr, colnum)
   end
end

Gtk.TreeViewColumn._custom = { cell = {} }
function Gtk.TreeViewColumn._custom.cell.set(column, cell)
   local pack = cell.pack == 'end' and Gtk.TreeViewColumn.pack_start
      or Gtk.TreeViewColumn.pack_end
   pack(column, cell[1], cell.expand == nil and true or cell.expand)
   if cell[2] then
      Gtk.TreeViewColumn.set_attributes(column, cell[1], cell[2])
   end
end

-------------------- Gtk.TreeView

Gtk.TreeView._custom = { columns = {} }
function Gtk.TreeView._custom.columns.set(treeview, columns)
   for i = 1, #columns do Gtk.TreeView.append_column(treeview, columns[i]) end
end

-------------------- Gtk.Dialog

Gtk.Dialog._custom = { buttons = {}, flags = {} }
function Gtk.Dialog._custom.buttons.set(dialog, buttons)
   for label, response in pairs(buttons) do
      Gtk.Dialog.add_button(dialog, label, response)
   end
end

-------------------- Gtk.FileChooser

Gtk.FileChooser._custom = { file = {}, filename = {} }
function Gtk.FileChooser._custom.file.get(filechooser)
   return Gtk.FileChooser.get_file(filechooser)
end

function Gtk.FileChooser._custom.file.set(filechooser, file)
   return Gtk.FileChooser.set_file(filechooser, file)
end

function Gtk.FileChooser._custom.filename.get(filechooser)
   return Gtk.FileChooser.get_filename(filechooser)
end

function Gtk.FileChooser._custom.filename.set(filechooser, filename)
   return Gtk.FileChooser.set_filename(filechooser, filename)
end

-------------------- Gtk.FileFilter

Gtk.FileFilter._custom = { name = {} , pattern = {}, mime_type = {} }
function Gtk.FileFilter._custom.name.get(filefilter)
   return Gtk.FileFilter.get_name(filefilter)
end

function Gtk.FileFilter._custom.name.set(filefilter, name)
   return Gtk.FileFilter.set_name(filefilter, name)
end

local function filefilter_add(filefilter, item, func)
   if type(item) == 'table' then
      for i = 1, #item do
	 func(filefilter, item[i])
      end
   else
      func(filefilter, item)
   end
end

function Gtk.FileFilter._custom.pattern.set(filefilter, pattern)
   filefilter_add(filefilter, pattern, Gtk.FileFilter.add_pattern)
end

function Gtk.FileFilter._custom.mime_type.set(filefilter, mime_type)
   filefilter_add(filefilter, mime_type, Gtk.FileFilter.add_mime_type)
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

Gtk.Builder._custom = { objects = {}, file = {}, string = {}, connect = {} }
function Gtk.Builder._custom.objects.get(builder)
   -- Get all objects and add metatable for resolving by name.
   local objects = builder:get_objects()
   objects._builder = builder
   return setmetatable(objects, builder_objects_mt)
end

local function builder_add(builder, vals, func)
   if type(vals) == 'table' then
      for i = 1, #vals do assert(func(builder, vals[i])) end
   else
      assert(func(builder, vals))
   end
end

function Gtk.Builder._custom.file.set(builder, vals)
   builder_add(builder, vals, Gtk.Builder.add_from_file)
end

function Gtk.Builder._custom.string.set(builder, vals)
   builder_add(builder, vals, Gtk.Builder.add_from_string)
end

function Gtk.Builder._custom.connect.set(builder, target)
   Gtk.Builder.connect_signals_full(
      builder, function(_, object, signal, handler)
		  object['on_' .. signal:gsub('%-', '_')] =
		  function(_, ...)
		     return target[handler](target, ...)
		  end
	    end)
end

-- Initialize GTK.
Gtk.init()
