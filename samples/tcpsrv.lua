#! /usr/bin/env lua

--
-- Sample TCP echo server. Listens on 3333 port
-- Use netcat to send strings to server (ncat 127.0.0.1 3333)
--

local lgi = require 'lgi'
local Gio = lgi.Gio
local assert = lgi.assert

local app = Gio.Application{application_id = 'org.lgi.samples.tcptest',
                            flags = Gio.ApplicationFlags.NON_UNIQUE}

local service = Gio.SocketService.new()
service:add_address(
   Gio.InetSocketAddress.new_from_string("127.0.0.1", 3333),
   Gio.SocketType.STREAM,
   Gio.SocketProtocol.TCP)

local function do_echo_connection(conn, istream, ostream)
   local wrote, err = ostream:async_write_all("Connected\n")
   assert(wrote >= 0, err)
   while true do
      local bytes = assert(istream:async_read_bytes(4096))
      if bytes:get_size() == 0 then break end
      local data = bytes.data:gsub("\n", "")
      print(string.format("Data: %s", data))
      wrote, err = ostream:async_write_all(bytes.data)
      assert(wrote >= 0, err)
   end
   print("Closing connection")
   conn:async_close()
end

function service:on_incoming(conn)
   local istream = assert(conn:get_input_stream())
   local ostream = assert(conn:get_output_stream())
   local rc = conn:get_remote_address()
   print(string.format("Incoming connection from %s:%s",
		       rc:get_address():to_string(),
		       rc:get_port()))
   Gio.Async.start(do_echo_connection)(conn, istream, ostream)
   return false
end

function app:on_activate()
   app:hold()
end

app:run({arg[0]})
