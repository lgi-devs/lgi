#! /usr/bin/env lua

--
-- Sample GStreamer application, port of public Vala GStreamer Audio
-- Stream Example (http://live.gnome.org/Vala/GStreamerSample)
--

require 'lgi'
local GLib = require 'lgi.GLib'
local Gtk = require 'lgi.Gtk'
local Gst = require 'lgi.Gst'

Gtk.init()
Gst.init()

local function bus_callback(bus, message)
   if message.type == Gst.MessageType.ERROR then
      print('Error:', message:parse_error().message)
      Gtk.main_quit()
   elseif message.type == Gst.MessageType.EOS then
      print 'end of stream'
   elseif message.type == Gst.MessageType.STATE_CHANGED then
      local old, new, pending = message:parse_state_changed()
      print(string.format('state changed: %s->%s:%s',
			  Gst.State[old] or tostring(old),
			  Gst.State[new] or tostring(new),
			  Gst.State[pending] or tostring(pending)))
   elseif message.type == Gst.MessageType.TAG then
      print('taglist found')
   end

   return true
end

local play = Gst.ElementFactory.make('playbin', 'play')
play.uri = 'http://streamer-dtc-aa02.somafm.com:80/stream/1018'
local bus = play.bus
bus:add_watch_full(GLib.PRIORITY_DEFAULT, bus_callback)
play:set_state(Gst.State.PLAYING)

-- Run the loop.
Gtk.main()
play:set_state(Gst.State.NULL)
