#! /usr/bin/env lua

--
-- Sample UDP server. Listens on 3333 port
-- Use netcat to send strings to server (ncat -u 127.0.0.1 3333)
--

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gio = lgi.Gio

local app = Gio.Application{application_id = 'org.v1993.udptest',
			    flags = Gio.ApplicationFlags.NON_UNIQUE}

local socket = lgi.Gio.Socket.new(Gio.SocketFamily.IPV4,
				  Gio.SocketType.DATAGRAM,
				  Gio.SocketProtocol.UDP)
local sa = lgi.Gio.InetSocketAddress.new(
   Gio.InetAddress.new_loopback(Gio.SocketFamily.IPV4),
   3333)
assert(socket:bind(sa, true))

do
   -- To avoid extra allocations
   local buf = require("lgi.core").bytes.new(4096)
   local source = socket:create_source(GLib.IOCondition.IN)
   source:set_callback(function()
	 print('Data incoming')
	 local len, src = socket:receive_from(buf)
	 if len > 0 then
	    print(('%s:%d %s'):format(src:get_address():to_string(),
				      src:get_port(),
				      tostring(buf):sub(1, len)))
	 else
	    print('Failed to read data')
	 end
	 return true
   end)

   source:attach(lgi.GLib.MainContext.default())
end

function app:on_activate()
   app:hold()
end

app:run({arg[0]})
