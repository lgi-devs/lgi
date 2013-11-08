return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local GdkPixbuf = lgi.GdkPixbuf
local cairo = lgi.cairo

local assert = lgi.assert

-- Load pixbuf images.
local background = assert(GdkPixbuf.Pixbuf.new_from_file(
			     dir:get_child('background.jpg'):get_path()))
local back_width, back_height = background.width, background.height
local images = {}
for _, name in ipairs {
   'apple-red.png', 'gnome-applets.png', 'gnome-calendar.png',
   'gnome-foot.png', 'gnome-gmush.png', 'gnome-gimp.png', 'gnome-gsame.png',
   'gnu-keys.png' } do
   images[#images + 1] = assert(GdkPixbuf.Pixbuf.new_from_file(
				   dir:get_child(name):get_path()))
end

local window = Gtk.Window {
   title = "Pixbufs",
   resizable = false,
   Gtk.DrawingArea {
      id = 'area',
      width = back_width,
      height = back_height,
   },
}

local frame = GdkPixbuf.Pixbuf.new('RGB', false, 8, back_width, back_height)
local area = window.child.area

function area:on_draw(cr)
   cr:set_source_pixbuf(frame, 0, 0)
   cr:paint()
   return true
end

local FRAME_DELAY = 50
local CYCLE_LEN = 60

local frame_num = 0
local sin, cos, pi, floor, abs, min, max
   = math.sin, math.cos, math.pi, math.floor, math.abs, math.min, math.max
local timeout_id = GLib.timeout_add(
   GLib.PRIORITY_DEFAULT, FRAME_DELAY, function()
      background:copy_area(0, 0, back_width, back_height, frame, 0, 0)

      local f = (frame_num % CYCLE_LEN) / CYCLE_LEN
      local xmid, ymid = back_width / 2, back_height / 2
      local radius = min(xmid, ymid) / 2
      local r1 = Gdk.Rectangle()
      local r2 = Gdk.Rectangle { x = 0, y = 0,
				 width = back_width, height = back_height }

      for i = 1, #images do
	 local ang = 2 * pi * (i / #images - f)
	 local iw, ih = images[i].width, images[i].height
	
	 local r = radius + (radius / 3) * sin(2 * pi * f)

	 local xpos = floor(xmid + r * cos(ang) - iw / 2 + 0.5)
	 local ypos = floor(ymid + r * sin(ang) - ih / 2 + 0.5)

	 local k = (i % 2 == 0) and sin(f * 2 * pi) or cos(f * 2 * pi)
	 k = max(2 * k * k, 0.25)

	 r1.x = xpos
	 r1.y = ypos
	 r1.width = iw * k
	 r1.height = ih * k

	 local dest = Gdk.Rectangle.intersect(r1, r2)
	 if dest then
	    local alpha = (i % 1 == 0) and sin(f * 2 * pi) or cos(f * 2 * pi)
	    images[i]:composite(frame, dest.x, dest.y, dest.width, dest.height,
				xpos, ypos, k, k,
				'NEAREST', max(127, abs(alpha)))
	 end
      end

      area:queue_draw()
      frame_num = frame_num + 1
      return GLib.SOURCE_CONTINUE
   end)

function window:on_destroy()
   GLib.source_remove(timeout_id)
end

window:show_all()
return window
end,

"Pixbufs",

table.concat {
   [[A Gdk.Pixbuf represents an image, normally in RGB or RGBA format. ]],
   [[Pixbufs are normally used to load files from disk and perform image ]],
   [[scaling.
]],
   [[This demo is not all that educational, but looks cool. It was ]],
   [[written by Extreme Pixbuf Hacker Federico Mena Quintero. It also ]],
   [[shows off how to use Gtk.DrawingArea to do a simple animation.
]],
   [[Look at the Image demo for additional pixbuf usage examples.]],
}
