#! /usr/bin/env lua

--
-- Sample GStreamer application, port of public Vala GStreamer Video
-- Example (http://live.gnome.org/Vala/GStreamerSample)
--

local lgi  = require 'lgi'
local GLib = lgi.GLib
local Gtk  = lgi.Gtk
local Gst  = lgi.Gst

---------------
-- GTK Stuff --
---------------

local vbox = Gtk.VBox.new(true, 0)
local drawing_area = Gtk.DrawingArea()

drawing_area:set_size_request(300, 150)
vbox:pack_start(drawing_area, true, true, 0)

local play_button = Gtk.Button.new_from_stock(Gtk.STOCK_MEDIA_PLAY)
local stop_button = Gtk.Button.new_from_stock(Gtk.STOCK_MEDIA_STOP)
local quit_button = Gtk.Button.new_from_stock(Gtk.STOCK_QUIT)

quit_button.on_clicked = Gtk.main_quit

local button_box = Gtk.HButtonBox.new()

button_box:add(play_button)
button_box:add(stop_button)
button_box:add(quit_button)
vbox:pack_start(button_box, false, true, 0)
local window = Gtk.Window.new(Gtk.WindowType.TOPLEVEL)

window:set_title('LGI Based Video Player')
window:add(vbox)

---------------
-- Gst Stuff --
---------------

local pipeline	 = Gst.Pipeline.new('mypipeline')
local src	 = Gst.ElementFactory.make('autovideosrc', 'videosrc')
local colorspace = Gst.ElementFactory.make('ffmpegcolorspace', 'colorspace')
local scale	 = Gst.ElementFactory.make('videoscale', 'scale')
local rate	 = Gst.ElementFactory.make('videorate', 'rate')
local sink	 = Gst.ElementFactory.make('autovideosink', 'sink')

pipeline:add_many(src, colorspace, scale, rate, sink)

src:link(colorspace)
colorspace:link(scale)
scale:link(rate)
rate:link(sink)

function play_button:on_clicked()
   pipeline:set_state(Gst.State.PLAYING)
end


function stop_button:on_clicked()
   pipeline:set_state(Gst.State.PAUSED)
end


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

pipeline.bus:add_watch(bus_callback)
window:show_all()
Gtk.main()

-- Must always set the pipeline to NULL before disposing it
pipeline:set_state(Gst.State.NULL)
