#! /usr/bin/env lua

--
-- Sample LGI application for Gio Streams.
--

local lgi = require 'lgi'
local bytes = require 'bytes'
local GLib = lgi.GLib
local Gio = lgi.Gio

local app = Gio.Application.new('org.lgi.samples.giostream', 0)

local function read_sync(file)
   local info = assert(file:query_info('standard::size', 0))
   local buffer = bytes.new(info:get_size())
   local stream = assert(file:read(nil))
   local read = assert(stream:read_all(buffer))
   return tostring(buffer):sub(1,read)
end

local function read_async(file)
   app:hold()
   local info = assert(file:async_query_info('standard::size', 0,
					     GLib.PRIORITY_DEFAULT))

   local stream = assert(file:async_read(GLib.PRIORITY_DEFAULT))
   local read_buffers = {}
   local remaining = info:get_size()
   while remaining > 0 do
      local buffer = bytes.new(remaining)
      local read_now, err = stream:async_read(buffer, GLib.PRIORITY_DEFAULT)
      assert(read_now >= 0, err)
      read_buffers[#read_buffers + 1] = tostring(buffer):sub(1, read_now)
      remaining = remaining - read_now
   end
   stream:async_close(GLib.PRIORITY_DEFAULT)
   app:release()
   return table.concat(read_buffers)
end

local function write_sync(file, contents)
   local stream = assert(file:create(0))
   assert(stream:write_all(contents))
end

local function write_async(file, contents)
   local stream = assert(file:async_create(0, GLib.PRIORITY_DEFAULT))
   local pos = 1
   while pos <= #contents do
      local wrote, err = stream:async_write(contents:sub(pos),
					    GLib.PRIORITY_DEFAULT)
      assert(wrote >= 0, err)
      pos = pos + wrote
   end
end

function app:on_activate()
   local source_file = Gio.File.new_for_commandline_arg(arg[0])

   local function perform(read_op, write_op, target_file)
      app:hold()
      print('Starting:', target_file:get_basename())
      local contents = read_op(source_file)
      target_file:delete()
      write_op(target_file, contents)
      local contents_copied = read_op(target_file)
      assert(contents == contents_copied)
      assert(target_file:delete())
      print('Success:', target_file:get_basename())
      app:release()
   end

   -- Perform sync variant of the test.
   perform(read_sync, write_sync, Gio.File.new_for_path('test-sync'))

   -- Perform async variant of the test inside the coroutine; start
   -- more of them.
   for i = 1, 10 do
      local coro = coroutine.create(perform)
      coroutine.resume(coro, read_async, write_async,
		       Gio.File.new_for_path('test-async-' .. i))
   end
end

app:run { arg[0], ... }
