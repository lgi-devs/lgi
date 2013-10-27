------------------------------------------------------------------------------
--
--  LGI Gio2 override module.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs, unpack =
   select, type, pairs, unpack or table.unpack
local coroutine = require 'coroutine'

local lgi = require 'lgi'
local Gio = lgi.Gio
local GObject = lgi.GObject

local core = require 'lgi.core'
local gi = core.gi

-- GOI < 1.30 did not map static factory method into interface
-- namespace.  The prominent example of this fault was that
-- Gio.File.new_for_path() had to be accessed as
-- Gio.file_new_for_path().  Create a compatibility layer to mask this
-- flaw.
for _, name in pairs { 'path', 'uri', 'commandline_arg' } do
   if not Gio.File['new_for_' .. name] then
      Gio.File['new_for_' .. name] = Gio['file_new_for_' .. name]
   end
end

-- Add 'async_' method handling.  Dynamically generates wrapper around
-- xxx_async()/xxx_finish() sequence using currently running
-- coroutine.
local inherited_element = GObject.Object._element
function GObject.Object:_element(object, name)
   local element, category = inherited_element(self, object, name)
   if element or not object then return element, category end

   -- Check, whether we have async_xxx request.
   local name_root = name:match('^async_(.+)$')
   if name_root then
      local async = inherited_element(self, object, name_root .. '_async')
      local finish = inherited_element(self, object, name_root .. '_finish')
      if async and finish then
	 local index = 0
	 for _, param in ipairs(async.params) do
	    if param['in'] then index = index + 1 end
	 end
	 return { async = async, finish = finish,
		  index = index }, '_async'
      end
   end
end

function GObject.Object:_access_async(object, data, ...)
   if select('#', ...) > 0 then
      error(("%s: `%s' not writable"):format(
	       core.object.query(object, 'repo')._name, name))
   end

   -- Generate wrapper method calling _async/_finish pair automatically.
   return function(...)
      local args = { ... }
      args[data.index] = coroutine.running()
      data.async(unpack(args, 1, data.index))
      return data.finish(coroutine.yield())
   end
end

-- Older versions of gio did not annotate input stream methods as
-- taking an array.  Apply workaround.
-- https://github.com/pavouk/lgi/issues/59
for _, name in pairs { 'read', 'read_all', 'read_async' } do
   local raw_read = Gio.InputStream[name]
   if gi.Gio.InputStream.methods[name].args[1].typeinfo.tag ~= 'array' then
      Gio.InputStream[name] = function(self, buffer, ...)
	 return raw_read(self, buffer, #buffer, ...)
      end
      if name == 'read_async' then
	 raw_finish = Gio.InputStream.read_finish
	 function Gio.InputStream.async_read(stream, buffer, prio, cancellable)
	    raw_read(stream, buffer, prio, cancellable, coroutine.running())
	    return raw_finish(coroutine.yield())
	 end
      end
   end
end
