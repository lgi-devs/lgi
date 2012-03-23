return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local function create_bbox(orientation, title, spacing, layout)
   return Gtk.Frame {
      label = title,
      Gtk.ButtonBox {
	 orientation = orientation,
	 border_width = 5,
	 layout_style = layout,
	 spacing = spacing,
	 Gtk.Button { use_stock = true, label = Gtk.STOCK_OK },
	 Gtk.Button { use_stock = true, label = Gtk.STOCK_CANCEL },
	 Gtk.Button { use_stock = true, label = Gtk.STOCK_HELP }
      },
   }
end

local window = Gtk.Window {
   title = "Button Boxes",
   border_width = 10,
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.Frame {
	 label = "Horizontal Button Boxes",
	 Gtk.Box {
	    orientation = 'VERTICAL',
	    border_width = 10,
	    create_bbox('HORIZONTAL', "Spread", 40, 'SPREAD'),
	    create_bbox('HORIZONTAL', "Edge", 40, 'EDGE'),
	    create_bbox('HORIZONTAL', "Start", 40, 'START'),
	    create_bbox('HORIZONTAL', "End", 40, 'END')
	 },
      },
      Gtk.Frame {
	 label = "Vertical Button Boxes",
	 Gtk.Box {
	    orientation = 'HORIZONTAL',
	    border_width = 10,
	    create_bbox('VERTICAL', "Spread", 30, 'SPREAD'),
	    create_bbox('VERTICAL', "Edge", 30, 'EDGE'),
	    create_bbox('VERTICAL', "Start", 30, 'START'),
	    create_bbox('VERTICAL', "End", 30, 'END')
	 },
      },
   }
}

window:show_all()
return window
end,

"Button Boxes",

"The Button Box widgets are used to arrange buttons with padding."
