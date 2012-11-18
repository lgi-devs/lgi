return function(parent, dir)

local math = require 'math'
local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local GtkDemo = lgi.GtkDemo

local log = lgi.log.domain 'gtk-demo'

GtkDemo:class('RotatedBin', Gtk.Bin)

function GtkDemo.RotatedBin:_init()
   self.has_window = true
   self.priv.angle = 0
end

local function to_child(bin, widget_x, widget_y)
   local s, c = math.sin(bin.priv.angle), math.cos(bin.priv.angle)
   local child_area = bin.priv.child.allocation
   local w = c * child_area.width + s * child_area.height
   local h = s * child_area.width + c * child_area.height

   local x = widget_x - w / 2
   local y = widget_y - h / 2

   local xr = x * c + y * s
   local yr = y * c - x * s

   return xr + child_area.width / 2, yr + child_area.height / 2
end

local function to_parent(bin, offscreen_x, offscreen_y)
   local s, c = math.sin(bin.priv.angle), math.cos(bin.priv.angle)
   local child_area = bin.priv.child.allocation

   local w = c * child_area.width + s * child_area.height
   local h = s * child_area.width + c * child_area.height

   local x = offscreen_x - child_area.width / 2
   local y = offscreen_y - child_area.height / 2

   local xr = x * c - y * s
   local yr = x * s + y * c

   return xr + child_area.width - w / 2, yr + child_area.height - h / 2
end

function GtkDemo.RotatedBin:do_realize()
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

function GtkDemo.RotatedBin:do_unrealize()
   -- Destroy offscreen window.
   self.priv.offscreen_window.widget = nil
   self.priv.offscreen_window:destroy()
   self.priv.offscreen_window = nil

   -- Chain to parent.
   GtkDemo.RotatedBin._parent.do_unrealize(self)
end

function GtkDemo.RotatedBin:do_child_type()
   return self.priv.child and GObject.Type.NONE or Gtk.Widget
end

function GtkDemo.RotatedBin:do_add(widget)
   if not self.priv.child then
      if self.priv.offscreen_window then
	 widget:set_parent_window(self.priv.offscreen_window)
      end
      widget:set_parent(self)
      self.priv.child = widget
   else
      log.warning("GtkDemo.RotatedBin cannot have more than one child")
   end
end

function GtkDemo.RotatedBin:do_remove(widget)
   local was_visible = widget.visible
   if self.priv.child == widget then
      widget:unparent()
      self.priv.child = nil
      if was_visible and self.visible then
	 self:queue_resize()
      end
   end
end

function GtkDemo.RotatedBin:do_forall(include_internals, callback)
   if self.priv.child then
      callback(self.priv.child, callback.user_data)
   end
end

function GtkDemo.RotatedBin:set_angle(angle)
   self.priv.angle = angle
   self:queue_resize()
   self.priv.offscreen_window:geometry_changed()
end

local function size_request(self)
   local child_requisition = Gtk.Requisition()
   if self.priv.child and self.priv.child.visible then
      child_requisition = self.priv.child:get_preferred_size()
   end

   local s, c = math.sin(self.priv.angle), math.cos(self.priv.angle)
   local w = c * child_requisition.width + s * child_requisition.height
   local h = s * child_requisition.width + c * child_requisition.height
   return w + 2 * self.border_width, h + 2 * self.border_width
end

function GtkDemo.RotatedBin:do_get_preferred_width()
   local w, h = size_request(self)
   return w, w
end

function GtkDemo.RotatedBin:do_get_preferred_height()
   local w, h = size_request(self)
   return h, h
end

function GtkDemo.RotatedBin:do_size_allocate(allocation)
   self:set_allocation(allocation)

   local w = allocation.width - self.border_width * 2
   local h = allocation.height - self.border_width * 2

   if self.realized then
      self.window:move_resize(allocation.x + self.border_width,
			      allocation.y + self.border_width,
			      w, h)
   end

   if self.priv.child and self.priv.child.visible then
      local s, c = math.sin(self.priv.angle), math.cos(self.priv.angle)
      local child_requisition = self.priv.child:get_preferred_size()
      local child_allocation = Gtk.Allocation {
	 height = child_requisition.height }
      if c == 0 then child_allocation.width = h / s
      elseif s == 0 then child_allocation.width = w / c
      else child_allocation.width = math.min(
	    (w - s * child_allocation.height) / c,
	    (h - c * child_allocation.width) / s)
      end
      if self.realized then
	 self.priv.offscreen_window:move_resize(child_allocation.x,
						child_allocation.y,
						child_allocation.width,
						child_allocation.height)
      end
      child_allocation.x = 0
      child_allocation.y = 0
      self.priv.child:size_allocate(child_allocation)
   end
end

function GtkDemo.RotatedBin:do_damage(event)
   self.window:invalidate_rect(nil, false)
   return true
end

function GtkDemo.RotatedBin:_class_init()
   -- Unfortunately, damage-event signal does not have virtual
   -- function associated, so we have to go through following funky
   -- dance to install default signal handler.
   GObject.signal_override_class_closure(
      GObject.signal_lookup('damage-event', Gtk.Widget),
      GtkDemo.RotatedBin,
      GObject.Closure(GtkDemo.RotatedBin.do_damage,
		      Gtk.Widget.on_damage_event))
end

function GtkDemo.RotatedBin:do_draw(cr)
   if cr:should_draw_window(self.window) then
      if self.priv.child and self.priv.child.visible then
	 local surface = Gdk.offscreen_window_get_surface(
	    self.priv.offscreen_window)
	 local child_area = self.priv.child.allocation

	 -- transform
	 local s, c = math.sin(self.priv.angle), math.cos(self.priv.angle)
	 local w = c * child_area.width + s * child_area.height
	 local h = s * child_area.width + c * child_area.height

	 cr:translate((w - child_area.width) / 2,
		      (h - child_area.height) / 2)
	 cr:translate(child_area.width / 2, child_area.height / 2)
	 cr:rotate(self.priv.angle)
	 cr:translate(-child_area.width / 2, -child_area.height / 2)

	 -- clip
	 cr:rectangle(0, 0,
		      self.priv.offscreen_window:get_width(),
		      self.priv.offscreen_window:get_height())
	 cr:clip()

	 -- paint
	 cr:set_source_surface(surface, 0, 0)
	 cr:paint()
      end
   end

   if cr:should_draw_window(self.priv.offscreen_window) then
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
   title = "Rotated widget",
   border_width = 10,
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.Scale {
	 id = 'scale',
	 orientation = 'HORIZONTAL',
	 adjustment = Gtk.Adjustment {
	    lower = 0,
	    upper = math.pi / 2,
	    step_increment = 0.01,
	 },
	 draw_value = false,
      },
      GtkDemo.RotatedBin {
	 id = 'bin',
	 Gtk.Button {
	    label = "A Button",
	    expand = true,
	 },
      },
   }
}

function window.child.scale:on_value_changed()
   window.child.bin:set_angle(self.adjustment.value)
end

window:override_background_color(0, Gdk.RGBA.parse('black'))

window:show_all()
return window
end,

"Offscreen windows/Rotated button",

table.concat {
   [[Offscreen windows can be used to transform parts or a ]],
   [[widget hierarchy. Note that the rotated button is fully functional.]],
}
