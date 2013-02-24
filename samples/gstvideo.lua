#! /usr/bin/env lua

--
-- Sample GStreamer application, based on public Vala GStreamer Video
-- Example (http://live.gnome.org/Vala/GStreamerSample)
--

local lgi  = require 'lgi'
local GLib = lgi.GLib
local Gtk  = lgi.Gtk
local GdkX11 = lgi.GdkX11
local Gst  = lgi.Gst
if tonumber(Gst._version) >= 1.0 then
   local GstVideo = lgi.GstVideo
end

local app = Gtk.Application { application_id = 'org.lgi.samples.gstvideo' }

local window = Gtk.Window {
   title = "LGI Based Video Player",
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.DrawingArea {
	 id = 'video',
	 expand = true,
	 width = 300,
	 height = 150,
      },
      Gtk.ButtonBox {
	 orientation = 'HORIZONTAL',
	 Gtk.Button {
	    id = 'play',
	    use_stock = true,
	    label = Gtk.STOCK_MEDIA_PLAY,
	 },
	 Gtk.Button {
	    id = 'stop',
	    use_stock = true,
	    sensitive = false,
	    label = Gtk.STOCK_MEDIA_STOP,
	 },
	 Gtk.Button {
	    id = 'quit',
	    use_stock = true,
	    label = Gtk.STOCK_QUIT,
	 },
      },
   }
}

function window.child.quit:on_clicked()
   window:destroy()
end

local pipeline = Gst.Pipeline.new('mypipeline')
local src = Gst.ElementFactory.make('autovideosrc', 'videosrc')
local colorspace = Gst.ElementFactory.make('videoconvert', 'colorspace')
                or Gst.ElementFactory.make('ffmpegcolorspace', 'colorspace')
local scale = Gst.ElementFactory.make('videoscale', 'scale')
local rate = Gst.ElementFactory.make('videorate', 'rate')
local sink = Gst.ElementFactory.make('xvimagesink', 'sink')

pipeline:add_many(src, colorspace, scale, rate, sink)
src:link_many(colorspace, scale, rate, sink)

function window.child.play:on_clicked()
   pipeline.state = 'PLAYING'
end

function window.child.stop:on_clicked()
   pipeline.state = 'PAUSED'
end

local function bus_callback(bus, message)
   if message.type.ERROR then
      print('Error:', message:parse_error().message)
      Gtk.main_quit()
   end
   if message.type.EOS then
      print 'end of stream'
   end
   if message.type.STATE_CHANGED then
      local old, new, pending = message:parse_state_changed()
      print(string.format('state changed: %s->%s:%s', old, new, pending))

      -- Set up sensitive state on buttons according to current state.
      -- Note that this is forwarded to mainloop, because bus callback
      -- can be called in some side thread and Gtk might not like to
      -- be controlled from other than main thread on some platforms.
      GLib.idle_add(GLib.PRIORITY_DEFAULT, function()
	 window.child.play.sensitive = (new ~= 'PLAYING')
	 window.child.stop.sensitive = (new == 'PLAYING')
	 return GLib.SOURCE_REMOVE
      end)
   end
   if message.type.TAG then
      message:parse_tag():foreach(
	 function(list, tag)
	    print(('tag: %s = %s'):format(tag, tostring(list:get(tag))))
	 end)
   end
   return true
end

function window.child.video:on_realize()
   -- Retarget video output to the drawingarea.
   sink:set_window_handle(self.window:get_xid())
end


function app:on_activate()
   window.application = app
   pipeline.bus:add_watch(GLib.PRIORITY_DEFAULT, bus_callback)
   window:show_all()
end

app:run { arg[0], ... }

-- Must always set the pipeline to NULL before disposing it
pipeline.state = 'NULL'
