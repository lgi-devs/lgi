#! /usr/bin/env lua

--
-- Sample TCP echo server. Listens on 3333 port
-- Use netcat to send strings to server (ncat 127.0.0.1 3333)
--

local lgi = require 'lgi'
local Gio = lgi.Gio

local app = Gio.Application{application_id = 'org.test.tcptest',
                            flags = Gio.ApplicationFlags.NON_UNIQUE}

local service = Gio.SocketService.new()
service:add_address(
   Gio.InetSocketAddress.new_from_string("127.0.0.1", 3333),
   Gio.SocketType.STREAM,
   Gio.SocketProtocol.TCP)

local function get_message(conn, istream, ostream)
   ostream:async_write("> ")
   local bytes = istream:async_read_bytes(4096)
   while bytes:get_size() > 0 do
      print(string.format("Data: %s",
			  bytes.data:sub(1, bytes:get_size()-1)))
      ostream:async_write(bytes.data)
      ostream:async_write("> ")
      bytes = istream:async_read_bytes(4096)
   end
   print("Closing connection")
   conn:close()
end

function service:on_incoming(conn)
   local istream = conn:get_input_stream()
   local ostream = conn:get_output_stream()
   local rc = conn:get_remote_address()
   print(string.format("Incoming connection from %s:%s",
		       rc:get_address():to_string(),
		       rc:get_port()))
   Gio.Async.call(function(ostream)
	 ostream:async_write("Connected\n")
   end)(ostream)
   Gio.Async.start(get_message)(conn, istream, ostream)
   return false
end

service:start()

function app:on_activate()
   app:hold()
end

app:run({arg[0]})
