#! /usr/bin/env lua

--
-- Sample LGI application for Gio Streams.
--

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gio = lgi.Gio

local log = lgi.log.domain('lgiostream')

local app = Gio.Application.new('org.lgi.samples.giostream', 0)

local function read_sync(file)
   local info = assert(file:query_info('standard::size', 0))
   local buffer = lgi.Buffer(info:get_size())
   local stream = assert(file:read(nil))
   local ok, read = assert(stream:read_all(buffer, #buffer))
   return tostring(buffer):sub(1,read)
end

local function read_async(file)
   app:hold()
   file:query_info_async('standard::size', 0, GLib.PRIORITY_DEFAULT, nil,
			 coroutine.running())
   local info = assert(file.query_info_finish(coroutine.yield()))
   local buffer = lgi.Buffer(info:get_size())
   file:read_async(GLib.PRIORITY_DEFAULT, nil, coroutine.running())
   local stream = assert(file.read_finish(coroutine.yield()))
   local read_buffers = {}
   local remaining = #buffer
   while remaining > 0 do
      stream:read_async(buffer, remaining, GLib.PRIORITY_DEFAULT, nil,
			coroutine.running())
      local read_now, err = stream.read_finish(coroutine.yield())
      assert(read_now >= 0, err)
      read_buffers[#read_buffers + 1] = tostring(buffer):sub(1, read_now)
      remaining = remaining - read_now
   end
   app:release()
   return table.concat(read_buffers)
end

local function write_sync(file, contents)
   local stream = assert(file:create(0))
   assert(stream:write_all(contents))
end

local function write_async(file, contents)
   file:create_async(0, GLib.PRIORITY_DEFAULT, nil, coroutine.running())
   local stream = assert(file.create_finish(coroutine.yield()))
   local pos = 1
   while pos <= #contents do
      stream:write_async(contents:sub(pos), GLib.PRIORITY_DEFAULT, nil,
			 coroutine.running())
      local wrote, err = stream.write_finish(coroutine.yield())
      assert(wrote >= 0, err)
      pos = pos + wrote
   end
end

function app:on_activate()
   local source_file = Gio.File.new_for_commandline_arg(arg[0])

   local function perform(read_op, write_op, target_file)
      app:hold()
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
