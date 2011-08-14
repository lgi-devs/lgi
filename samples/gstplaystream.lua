#! /usr/bin/env lua

--
-- Sample GStreamer application, port of public Vala GStreamer Audio
-- Stream Example (http://live.gnome.org/Vala/GStreamerSample)
--

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gtk = lgi.Gtk
local Gst = lgi.Gst

local function bus_callback(bus, message)
   if message.type == Gst.MessageType.ERROR then
      print('Error:', message:parse_error().message)
      Gtk.main_quit()
   elseif message.type == Gst.MessageType.EOS then
      print 'end of stream'
   elseif message.type == Gst.MessageType.STATE_CHANGED then
      local old, new, pending = message:parse_state_changed()
      print(string.format('state changed: %s->%s:%s',
			  Gst.State[old], Gst.State[new], Gst.State[pending]))
   elseif message.type == Gst.MessageType.TAG then
      message:parse_tag():foreach(
	 function(list, tag)
	    print(('tag: %s = %s'):format(tag, tostring(list:get(tag))))
	 end)
   end

   return true
end

local play = Gst.ElementFactory.make('playbin', 'play')
play.uri = 'http://streamer-dtc-aa02.somafm.com:80/stream/1018'
--play.uri = 'http://www.cybertechmedia.com/samples/raycharles.mov'
local bus = play:get_bus()
bus:add_watch(bus_callback)
play:set_state(Gst.State.PLAYING)

-- Run the loop.
Gtk.main()
play:set_state(Gst.State.NULL)
