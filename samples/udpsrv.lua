#! /usr/bin/env lua

--
-- Sample UDP server. Listens on 3333 port
-- Use netcat to send strings to server (ncat -u 127.0.0.1 3333)
--

local lgi = require 'lgi'
local core = require 'lgi.core'
local GLib = lgi.GLib
local Gio = lgi.Gio
local assert = lgi.assert

local app = Gio.Application{application_id = 'org.lgi.samples.udptest',
			    flags = Gio.ApplicationFlags.NON_UNIQUE}

local socket = Gio.Socket.new(Gio.SocketFamily.IPV4,
			      Gio.SocketType.DATAGRAM,
			      Gio.SocketProtocol.UDP)
local sa = Gio.InetSocketAddress.new_from_string("127.0.0.1", 3333)
assert(socket:bind(sa, true))

local buf = core.bytes.new(4096)
local source = assert(socket:create_source(GLib.IOCondition.IN))

source:set_callback(function()
      local len, src = assert(socket:receive_from(buf))
      if len > 0 then
	 local data = tostring(buf):sub(1, len):gsub("\n", "")
	 print(string.format("%s:%d %s",
			     src:get_address():to_string(),
			     src:get_port(),
			     data))
      else
	 print('Failed to read data')
      end
      return true
end)

source:attach(GLib.MainContext.default())

function app:on_activate()
   app:hold()
end

app:run({arg[0]})
