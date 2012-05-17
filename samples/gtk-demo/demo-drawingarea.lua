return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local cairo = lgi.cairo

local window = Gtk.Window {
   title = "Drawing Area",
   border_width = 8,
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 8,
      border_width = 8,
      Gtk.Label {
	 use_markup = true,
	 label = "<u>Checkerboard pattern</u>",
      },
      Gtk.Frame {
	 shadow_type = 'IN',
	 expand = true,
	 Gtk.DrawingArea {
	    id = 'checkerboard',
	    width = 100,
	    height = 100,
	 },
      },
      Gtk.Label {
	 use_markup = true,
	 label = "<u>Scribble area</u>",
      },
      Gtk.Frame {
	 shadow_type = 'IN',
	 expand = true,
	 Gtk.DrawingArea {
	    id = 'scribble',
	    width = 100,
	    height = 100,
	 },
      },
   },
}

-- Handling of checkerboard area.
local SPACING = 2
local CHECK_SIZE = 10
function window.child.checkerboard:on_draw(cr)
   local i = SPACING
   local xcount = 0
   while i < self.width do
      local j = SPACING
      local ycount = xcount % 2
      while j < self.height do
	 if ycount % 2 ~= 0 then
	    cr:set_source_rgb(0.45777, 0, 0.45777)
	 else
	    cr:set_source_rgb(1, 1, 1)
	 end
	 cr:rectangle(i, j, CHECK_SIZE, CHECK_SIZE)
	 cr:fill()
	 j = j + CHECK_SIZE + SPACING
	 ycount = ycount + 1
      end
      i = i + CHECK_SIZE + SPACING
      xcount = xcount + 1
   end
   return true
end

-- Setup and handling of scribble area.
local scribble = window.child.scribble

local surface
function scribble:on_configure_event(event)
   -- Create new surface of appropriate size to store the scribbles.
   local allocation = self.allocation
   surface = self.window:create_similar_surface(
      'COLOR', allocation.width, allocation.height)

   -- Initialize the surface to white.
   local cr = cairo.Context.create(surface)
   cr:set_source_rgb(1, 1, 1)
   cr:paint()
   return true
end

function scribble:on_draw(cr)
   -- Redraw the screen from the buffer.
   cr:set_source_surface(surface, 0, 0)
   cr:paint()
   return true
end

-- Draw a rectangle on the scribble surface.
local function draw_brush(widget, x, y)
   local update_rect = Gdk.Rectangle { x = x - 3, y = y - 3,
				       width = 6, height = 6 }

   -- Paint to the scribble surface
   local cr = cairo.Context(surface)
   cr:rectangle(update_rect)
   cr:fill()

   -- Invalidate affected region of the paint area.
   widget.window:invalidate_rect(update_rect, false)
end

function scribble:on_motion_notify_event(event)

   -- This call is very important; it requests the next motion event.
   -- If you don't call Gdk.Window.get_pointer() you'll only get
   -- a single motion event. The reason is that we specified
   -- Gdk.EventMask.POINTER_MOTION_HINT_MASK to Gtk.Widget.add_events().
   -- If we hadn't specified that, we could just use event.x, event.y
   -- as the pointer location. But we'd also get deluged in events.
   -- By requesting the next event as we handle the current one,
   -- we avoid getting a huge number of events faster than we
   -- can cope.
   local _, x, y, state = event.window:get_device_position(event.device)
   if state.BUTTON1_MASK then
      draw_brush(self, x, y)
   end

   return true
end

function scribble:on_button_press_event(event)
   if event.button == Gdk.BUTTON_PRIMARY then
      draw_brush(self, event.x, event.y)
   end
   return true
end

scribble:add_events(Gdk.EventMask {
		       'LEAVE_NOTIFY_MASK',
		       'BUTTON_PRESS_MASK',
		       'POINTER_MOTION_MASK',
		       'POINTER_MOTION_HINT_MASK' })

window:show_all()
return window
end,

"Drawing Area",

table.concat {
   [[Gtk.DrawingArea is a blank area where you can draw custom displays ]],
   [[of various kinds.
]],
   [[This demo has two drawing areas. The checkerboard area shows how ]],
   [[you can just draw something; all you have to do is write a signal ]],
   [[handler for expose_event, as shown here.
]],
   [[The "scribble" area is a bit more advanced, and shows how to handle ]],
   [[events such as button presses and mouse motion. Click the mouse ]],
   [[and drag in the scribble area to draw squiggles. Resize the window ]],
   [[to clear the area.]]
}
