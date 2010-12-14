------------------------------------------------------------------------------
--
--  LGI Gtk3 override module.
--
--  Copyright (c) 2010 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type = select, type
local lgi = require 'lgi'
local Gtk = lgi.Gtk

-------------------- Gtk.Container
local container_access_element = Gtk.Container._access_element
function Gtk.Container:_access_element(container, name, element, ...)
   if name == 'children' then
      if select('#', ...) == 0 then
	 -- Reading yields the table of all children.
	 return self.get_children(container)
      else
	 -- Writing adds new children.
	 local arg = ...
	 for i = 1, #arg do self.add(container, arg[i]) end
	 return
      end
   end

   return container_access_element(self, container, name, element, ...)
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
