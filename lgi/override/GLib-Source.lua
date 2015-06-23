------------------------------------------------------------------------------
--
--  lgi GLib Source support
--
--  Copyright (c) 2015 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local type, setmetatable, pairs = type, setmetatable, pairs

local lgi = require 'lgi'
local core = require 'lgi.core'
local gi = core.gi
local component = require 'lgi.component'
local record = require 'lgi.record'
local ffi = require 'lgi.ffi'
local ti = ffi.types

local GLib = lgi.GLib
local Source = GLib.Source
local SourceFuncs = GLib.SourceFuncs

SourceFuncs._field.prepare = {
   name = 'prepare',
   offset = SourceFuncs._field.prepare.offset,
   ret = ti.boolean, Source, { ti.int, dir = 'out' }
}
local source_new = Source._new
function Source:_new(funcs)
   if type(funcs) == 'table' then
      funcs = SourceFuncs(funcs)
   end
   function funcs.finalize(source)
      funcs = nil
   end
   return source_new(self, funcs, Source._size)
end
