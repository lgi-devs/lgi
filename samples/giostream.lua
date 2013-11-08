#! /usr/bin/env lua

--
-- Sample lgi application for Gio files, streams and usage of
-- Gio.Async It also serves as very crude timing 'benchmark' showing
-- overhead of assorted types operations.
--

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gio = lgi.Gio

local assert = lgi.assert

local app = Gio.Application { application_id = 'org.lgi.samples.giostream' }

local function read_lua(file)
   local stream = io.open(file:get_path(), 'r')
   local contents = stream:read('*a')
   stream:close()
   return contents
end

local function write_lua(file, contents)
   local stream = io.open(file:get_path(), 'w')
   stream:write(contents)
   stream:close()
end

local function read_sync(file)
   local info = assert(file:query_info('standard::size', 0))
   local stream = assert(file:read())
   local read_buffers = {}
   local remaining = info:get_size()
   while remaining > 0 do
      local buffer = assert(stream:read_bytes(remaining))
      table.insert(read_buffers, buffer.data)
      remaining = remaining - #buffer
   end
   assert(stream:close())
   return table.concat(read_buffers)
end

local function read_async(file)
   local info = assert(file:async_query_info('standard::size', 'NONE'))
   local stream = assert(file:async_read())
   local read_buffers = {}
   local remaining = info:get_size()
   while remaining > 0 do
      local buffer = assert(stream:async_read_bytes(remaining))
      table.insert(read_buffers, buffer.data)
      remaining = remaining - #buffer
   end
   stream:async_close()
   return table.concat(read_buffers)
end

local function write_sync(file, contents)
   local stream = assert(file:create('NONE'))
   local pos = 1
   while pos <= #contents do
      local wrote, err = stream:write_bytes(GLib.Bytes(contents:sub(pos)))
      assert(wrote >= 0, err)
      pos = pos + wrote
   end
end

local function write_async(file, contents)
   local stream = assert(file:async_create('NONE'))
   local pos = 1
   while pos <= #contents do
      local wrote, err = stream:async_write_bytes(GLib.Bytes(contents:sub(pos)))
      assert(wrote >= 0, err)
      pos = pos + wrote
   end
end


local source_file = Gio.File.new_for_commandline_arg(arg[0])
local function perform(read_op, write_op, target_file)
   app:hold()
   io.write('+')
   local contents = read_op(source_file)
   target_file:delete()
   write_op(target_file, contents)
   local contents_copied = read_op(target_file)
   assert(contents == contents_copied)
   assert(target_file:delete())
   io.write('.')
   app:release()
end

local count = 100
local timer = GLib.Timer()

-- Perform sync standard-lua variant of the test.
timer:reset()
for i = 1, count do
   perform(read_lua, write_lua, Gio.File.new_for_path('test-lua-' .. i))
end
print((("\n      Lua %0.2f secs"):format(timer:elapsed())))

-- Perform sync variant of the test.
timer:reset()
for i = 1, count do
   perform(read_sync, write_sync, Gio.File.new_for_path('test-sync-' .. i))
end
print((("\n     sync %0.2f secs"):format(timer:elapsed())))

-- Perform async variant of the test.
timer:reset()
for i = 1, count do
   Gio.Async.call(perform)(read_async, write_async,
			   Gio.File.new_for_path('test-async-' .. i))
end
print((("\n    async %0.2f secs"):format(timer:elapsed())))

-- Perform parallel variant of the test.
function app:on_activate()
   for i = 1, count do
      Gio.Async.start(perform)(read_async, write_async,
			       Gio.File.new_for_path('test-parallel-' .. i))
   end
end
timer:reset()
app:run(...)
print((("\n parallel %0.2f secs"):format(timer:elapsed())))
