return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local cairo = lgi.cairo
local Pango = lgi.Pango
local PangoCairo = lgi.PangoCairo

local RADIUS = 150
local N_WORDS = 5
local FONT = 'Serif 18'

local HEART = '♥'
local TEXT = 'I ♥ GTK+'

local window = Gtk.Window {
   title = "Rotated Text",
   default_width = 4 * RADIUS,
   default_height = 2 * RADIUS,
   Gtk.Box {
      orientation = 'HORIZONTAL',
      homogeneous = true,
      Gtk.DrawingArea {
	 id = 'circle',
      },
      Gtk.Label {
	 id = 'label',
	 angle = 45,
	 label = TEXT,
      },
   }
}

-- Override background color of circle drawing area.
window.child.circle:override_background_color(
   'NORMAL', Gdk.RGBA { red = 1, green = 1, blue = 1, alpha = 1 })

local function fancy_shape_renderer(cr, attr, do_path)
   cr:translate(cr:get_current_point())
   cr:scale(attr.ink_rect.width / Pango.SCALE,
	    attr.ink_rect.height / Pango.SCALE)

   -- Draw the manually.
   cr:move_to(0.5, 0)
   cr:line_to(0.9, -0.4)
   cr:curve_to(1.1, -0.8, 0.5, -0.9, 0.5, -0.5)
   cr:curve_to(0.5, -0.9, -0.1, -0.8, 0.1, -0.4)
   cr:close_path()

   if not do_path then
      cr:set_source_rgb(1, 0, 0)
      cr:fill()
   end
end

local function create_fancy_attr_list_for_layout(layout)
   -- Get font metrics and prepare fancy shape size.
   local ascent = layout.context:get_metrics(layout.font_description).ascent
   local rect = Pango.Rectangle { x = 0, width = ascent,
				  y = -ascent, height = ascent }

   -- Create attribute list, add specific shape renderer for every
   -- occurence of heart symbol.
   local attrs = Pango.AttrList()
   local start_index, end_index = 1, 1
   while true do
      start_index, end_index = TEXT:find(HEART, end_index + 1, true)
      if not start_index then break end
      local attr = Pango.Attribute.shape_new(rect, rect)
      attr.start_index = start_index - 1
      attr.end_index = end_index
      attrs:insert(attr)
   end
   return attrs
end

function window.child.circle:on_draw(cr)
   -- Create a cairo context and set up a transformation matrix so that
   -- the user space coordinates for the centered square where we draw
   -- are [-RADIUS, RADIUS], [-RADIUS, RADIUS]. We first center, then
   -- change the scale.
   local device_radius = math.min(self.width, self.height) / 2
   cr:translate(device_radius + (self.width - 2 * device_radius) / 2,
		device_radius + (self.height - 2 * device_radius) / 2)
   cr:scale(device_radius / RADIUS, device_radius / RADIUS)

   -- Create a subtle gradient source and use it.
   local pattern = cairo.Pattern.create_linear(-RADIUS, -RADIUS,
					       RADIUS, RADIUS)
   pattern:add_color_stop_rgb(0, 0.5, 0, 0)
   pattern:add_color_stop_rgb(1, 0, 0, 0.5)
   cr:set_source(pattern)

   -- Create a Pango.Context and set up our shape renderer.
   local context = self:create_pango_context()
   context.shape_renderer = fancy_shape_renderer

   -- Create a Pango.Layout, set the text, font and attributes.
   local layout = Pango.Layout.new(context)
   layout.text = TEXT
   layout.font_description = Pango.FontDescription.from_string(FONT)
   layout.attributes = create_fancy_attr_list_for_layout(layout)

   -- Draw the layout N_WORDS times in a circle.
   for i = 1, N_WORDS do
      -- Inform Pango to re-layout the text with the new transformation
      -- matrix.
      cr:update_layout(layout)

      local width, height = layout:get_pixel_size()
      cr:move_to(-width / 2, -RADIUS * 0.9)
      cr:show_layout(layout)

      -- Rotate for the next turn.
      cr:rotate(2 * math.pi / N_WORDS)
   end
end

-- Set up fancy stuff on the label.
local label = window.child.label
local layout = label:get_layout()
layout.context.shape_renderer = fancy_shape_renderer
label:set_attributes(create_fancy_attr_list_for_layout(layout))

window:show_all()
return window
end,

"Rotated Text",

table.concat {
   [[This demo shows how to use PangoCairo to draw rotated and transformed ]],
   [[text.  The right pane shows a rotated Gtk.Label widget.
]],
   [[In both cases, a custom PangoCairo shape renderer is installed ]],
   [[to draw a red heard using cairo drawing operations instead of ]],
   [[the Unicode heart character.]]
}
