------------------------------------------------------------------------------
--
--  LGI Gio2 override module.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs = select, type, pairs
local lgi = require 'lgi'
local core = require 'lgi._core'
local Gio = lgi.Gio
local GObject = lgi.GObject

-------------------- Gio.File
Gio.File._override = { contents = {} }
function Gio.File._override.contents.read(file)
   local ok, contents = assert(Gio.File.load_contents(file))
   return contents
end

-- Move new_for_xxx into Gio.File scope.
for _, name in ipairs { 'path', 'uri', 'commandline_arg' } do
   if not Gio.File._method['new_for_' .. name] then
      Gio.File._method['new_for_' .. name] = Gio['file_new_for_' .. name]
      Gio['file_new_for_' .. name] = nil
   end
end
Gio.File._method.new_for_parse_name = Gio.file_parse_name
Gio.file_parse_name = nil

local file_parse_name = Gio.File._method.parse_name
Gio.File._method.parse_name = nil
function Gio.File.new_for_parse_name(name)
   return file_parse_name(name)
end

-- Add accessor properties for filename's get_foo methods.
for _, name in pairs { 'basename', 'path', 'uri', 'parse_name' }  do
   Gio.File._override[name] = {
      get = function(file)
		return Gio.File['get_' .. name](file)
	     end }
end
