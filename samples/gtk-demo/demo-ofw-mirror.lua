return function(parent, dir)

local math = require 'math'
local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local cairo = lgi.cairo
local GtkDemo = lgi.GtkDemo

local log = lgi.log.domain 'gtk-demo'

GtkDemo:class('MirrorBin', Gtk.Bin)

function GtkDemo.MirrorBin:_init()
   self.has_window = true
end

local function to_child(bin, widget_x, widget_y)
   return widget_x, widget_y
end

local function to_parent(bin, offscreen_x, offscreen_y)
   return offscreen_x, offscreen_y
end

function GtkDemo.MirrorBin:do_realize()
   self.realized = true

   -- Create Gdk.Window and bind it with the widget.
   local events = self.events
   events.EXPOSURE_MASK = true
   events.POINTER_MOTION_MASK = true
   events.BUTTON_PRESS_MASK = true
   events.BUTTON_RELEASE_MASK = true
   events.SCROLL_MASK = true
   events.ENTER_NOTIFY_MASK = true
   events.LEAVE_NOTIFY_MASK = true

   local attributes = Gdk.WindowAttr {
      x = self.allocation.x + self.border_width,
      y = self.allocation.y + self.border_width,
      width = self.allocation.width - 2 * self.border_width,
      height = self.allocation.height - 2 * self.border_width,
      window_type = 'CHILD',
      event_mask = Gdk.EventMask(events),
      visual = self:get_visual(),
      wclass = 'INPUT_OUTPUT',
   }

   local window = Gdk.Window.new(self:get_parent_window(), attributes,
				 { 'X', 'Y', 'VISUAL' })
   self:set_window(window)
   window.widget = self

   local bin = self
   function window:on_pick_embedded_child(widget_x, widget_y)
      if bin.priv.child and bin.priv.child.visible then
	 local x, y = to_child(bin, widget_x, widget_y)
	 local child_area = bin.allocation
	 if x >= 0 and x < child_area.width
	    and y >= 0 and y < child_area.height then
	    return bin.priv.offscreen_window
	 end
      end
   end

   -- Create and hook up the offscreen window.
   attributes.window_type = 'OFFSCREEN'
   local child_requisition = Gtk.Requisition { width = 0, height = 0 }
   if self.priv.child and self.priv.child.visible then
      local child_allocation = self.priv.child.allocation
      attributes.width = child_allocation.width
      attributes.height = child_allocation.height
   end
   self.priv.offscreen_window = Gdk.Window.new(self.root_window, attributes,
						  { 'X', 'Y', 'VISUAL' })
   self.priv.offscreen_window.widget = self
   if self.priv.child then
      self.priv.child:set_parent_window(bin.priv.offscreen_window)
   end
   Gdk.offscreen_window_set_embedder(self.priv.offscreen_window, window)
   function self.priv.offscreen_window:on_to_embedder(offscreen_x, offscreen_y)
      return to_parent(bin, offscreen_x, offscreen_y)
   end
   function self.priv.offscreen_window:on_from_embedder(parent_x, parent_y)
      return to_child(bin, parent_x, parent_y)
   end

   -- Set background of the windows according to current context.
   self.style_context:set_background(window)
   self.style_context:set_background(self.priv.offscreen_window)
   self.priv.offscreen_window:show()
end

function GtkDemo.MirrorBin:do_unrealize()
   -- Destroy offscreen window.
   self.priv.offscreen_window.widget = nil
   self.priv.offscreen_window:destroy()
   self.priv.offscreen_window = nil

   -- Chain to parent.
   GtkDemo.MirrorBin._parent.do_unrealize(self)
end

function GtkDemo.MirrorBin:do_child_type()
   return self.priv.child and GObject.Type.NONE or Gtk.Widget
end

function GtkDemo.MirrorBin:do_add(widget)
   if not self.priv.child then
      if self.priv.offscreen_window then
	 widget:set_parent_window(self.priv.offscreen_window)
      end
      widget:set_parent(self)
      self.priv.child = widget
   else
      log.warning("GtkDemo.MirrorBin cannot have more than one child")
   end
end

function GtkDemo.MirrorBin:do_remove(widget)
   local was_visible = widget.visible
   if self.priv.child == widget then
      widget:unparent()
      self.priv.child = nil
      if was_visible and self.visible then
	 self:queue_resize()
      end
   end
end

function GtkDemo.MirrorBin:do_forall(include_internals, callback)
   if self.priv.child then
      callback(self.priv.child, callback.user_data)
   end
end

local function size_request(self)
   local child_requisition = Gtk.Requisition()
   if self.priv.child and self.priv.child.visible then
      child_requisition = self.priv.child:get_preferred_size()
   end

   local w = child_requisition.width + 10 + 2 * self.border_width
   local h = child_requisition.height + 10 + 2 * self.border_width
   return w, h
end

function GtkDemo.MirrorBin:do_get_preferred_width()
   local w, h = size_request(self)
   return w, w
end

function GtkDemo.MirrorBin:do_get_preferred_height()
   local w, h = size_request(self)
   return h, h
end

function GtkDemo.MirrorBin:do_size_allocate(allocation)
   self:set_allocation(allocation)

   local w = allocation.width - self.border_width * 2
   local h = allocation.height - self.border_width * 2

   if self.realized then
      self.window:move_resize(allocation.x + self.border_width,
			      allocation.y + self.border_width,
			      w, h)
   end

   if self.priv.child and self.priv.child.visible then
      local child_requisition = self.priv.child:get_preferred_size()
      local child_allocation = Gtk.Allocation {
	 height = child_requisition.height,
	 width = child_requisition.width
      }

      if self.realized then
	 self.priv.offscreen_window:move_resize(child_allocation.x,
						child_allocation.y,
						child_allocation.width,
						child_allocation.height)
      end
      self.priv.child:size_allocate(child_allocation)
   end
end

function GtkDemo.MirrorBin:do_damage(event)
   self.window:invalidate_rect(nil, false)
   return true
end

function GtkDemo.MirrorBin:_class_init()
   -- Unfortunately, damage-event signal does not have virtual
   -- function associated, so we have to go through following funky
   -- dance to install default signal handler.
   GObject.signal_override_class_closure(
      GObject.signal_lookup('damage-event', Gtk.Widget),
      GtkDemo.MirrorBin,
      GObject.Closure(GtkDemo.MirrorBin.do_damage,
		      Gtk.Widget.on_damage_event))
end

function GtkDemo.MirrorBin:do_draw(cr)
   if cr:should_draw_window(self.window) then
      if self.priv.child and self.priv.child.visible then
	 local surface = Gdk.offscreen_window_get_surface(
	    self.priv.offscreen_window)
	 local height = self.priv.offscreen_window:get_height()

	 -- Paint the offscreen child
	 cr:set_source_surface(surface, 0, 0)
	 cr:paint()

	 local matrix = cairo.Matrix {
	    xx = 1, yx = 0, xy = 0.3, yy = 1, x0 = 0, y0 = 0 }
	 matrix:scale(1, -1)
	 matrix:translate(-10, -3 * height, - 10)
	 cr:transform(matrix)

	 cr:set_source_surface(surface, 0, height)

	 -- Create linear gradient as mask-pattern to fade out the source
	 local mask = cairo.LinearPattern(0, height, 0, 2 * height)
	 mask:add_color_stop_rgba(0, 0, 0, 0, 0)
	 mask:add_color_stop_rgba(0.25, 0, 0, 0, 0.01)
	 mask:add_color_stop_rgba(0.5, 0, 0, 0, 0.25)
	 mask:add_color_stop_rgba(0.75, 0, 0, 0, 0.5)
	 mask:add_color_stop_rgba(1, 0, 0, 0, 1)

	 -- Paint the reflection
	 cr:mask(mask)
      end
   elseif cr:should_draw_window(self.priv.offscreen_window) then
      Gtk.render_background(self.style_context, cr, 0, 0,
			    self.priv.offscreen_window:get_width(),
			    self.priv.offscreen_window:get_height())
      if self.priv.child then
	 self:propagate_draw(self.priv.child, cr)
      end
   end

   return false
end

local window = Gtk.Window {
   title = "Effects",
   border_width = 10,
   Gtk.Box {
      orientation = 'VERTICAL',
      expand = true,
      GtkDemo.MirrorBin {
	 Gtk.Box {
	    orientation = 'HORIZONTAL',
	    spacing = 6,
	    Gtk.Button {
	       Gtk.Image {
		  stock = Gtk.STOCK_GO_BACK,
		  icon_size = 4,
	       },
	    },
	    Gtk.Entry {
	       expand = true,
	    },
	    Gtk.Button {
	       Gtk.Image {
		  stock = Gtk.STOCK_APPLY,
		  icon_size = 4,
	       },
	    },
	 },
      },
   },
}

window:show_all()
return window
end,

"Offscreen windows/Effects",

table.concat {
   [[Offscreen windows can be used to render elements multiple times ]],
   [[to achieve various effects.]]
}
