local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Goo = lgi.GooCanvas

local window = Gtk.Window {
   on_delete_event = Gtk.main_quit,
   Gtk.ScrolledWindow {
      shadow_type = 'IN',
      Goo.Canvas {
	 id = 'canvas',
	 width = 600, height = 450,
      }
   },
}

window:set_default_size(640, 600)
window:show_all()

window.child.canvas:set_bounds(0, 0, 1000, 1000)
local root = window.child.canvas.root_item

local rect_item = Goo.CanvasRect {
    parent = root,
    x = 100, y = 100,
    width = 400, height = 400,

    line_width = 10,
    stroke_color = 'yellow',
    fill_color = 'red',
    radius_x = 20,
    radius_y = 10,

    on_button_press_event = function ()
       print("rect item received button press event")
    end    
}

local text_item = Goo.CanvasText {
    parent = root,
    x = 300, y = 300,
    width = -1,

    text = "Hello World",
    anchor = 'CENTER',
    font = 'Sans 24',
}
text_item:rotate(45, 300, 300)

Gtk.main()
