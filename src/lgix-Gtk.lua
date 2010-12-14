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

-- Overrides for the builder.  Main tool is 'objects' pseudoproperty
-- which contains collection of all builder objects, and adds __index
-- method for retrieving object by name.
local builder_objects_mt = {}
function builder_objects_mt:__index(name)
   if type(name) == 'string' then
      return self._builder:get_object(name)
   end
end
local builder_access_element = Gtk.Builder._access_element
function Gtk.Builder:_access_element(instance, name, element, ...)
   -- Detect 'objects' property request.
   if name == 'objects' then
      assert(select('#', ...) == 0, "Gtk.Builder: 'objects' is not writable")

      -- Get all objects.
      local objects = instance:get_objects()
      objects._builder = instance

      -- Add metatable for resolving by name.
      return setmetatable(objects, builder_objects_mt)
   end

   -- Forward to original method.
   return builder_access_element(self, instance, name, element, ...)
end

-- Create helper static method, which creates builder, adds specified
-- file(s) as resources and returns its 'objects' property.
function Gtk.Builder.from_files(...)
   local builder, files = Gtk.Builder(), {...}
   for i = 1, #files do assert(builder:add_from_file(files[i])) end
   return builder.objects
end

-- Initialize GTK.
Gtk.init()
