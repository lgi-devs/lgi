return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Dialog {
   title = "Gtk.SizeGroup",
   transient_for = parent,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.NONE },
   },
   resizable = false,
   on_response = Gtk.Widget.destroy,
}

window:get_content_area():add(
   Gtk.Box {
      orientation = 'VERTICAL',
      border_width = 5,
      spacing = 5,
      Gtk.Frame {
	 label = "Color Options",
	 Gtk.Grid {
	    id = 'colors',
	    row_spacing = 5,
	    column_spacing = 10,
	 }
      },
      Gtk.Frame {
	 label = "Line Options",
	 Gtk.Grid {
	    id = 'lines',
	    row_spacing = 5,
	    column_spacing = 10,
	 }
      },
      Gtk.CheckButton {
	 id = 'enable_grouping',
	 label = "_Enable grouping",
	 use_underline = true,
	 active = true,
      },
   })

local size_group = Gtk.SizeGroup { mode = 'HORIZONTAL' }
local function add_row(grid, row, label, strings)
   local combo = Gtk.ComboBoxText {}
   for _, text in ipairs(strings) do
      combo:append_text(text)
   end
   combo.active = 0
   size_group:add_widget(combo)
   grid:add {
      left_attach = 0, top_attach = row,
      Gtk.Label {
	 label = label,
	 use_underline = true,
	 halign = 'START',
	 valign = 'END',
	 hexpand = true,
	 mnemonic_widget = combo,
      }
   }
   grid:add {
      left_attach = 1, top_attach = row,
      combo
   }
end

add_row(window.child.colors, 0, "_Foreground", {
	   "Red", "Green", "Blue",
	})
add_row(window.child.colors, 1, "_Background", {
	   "Red", "Green", "Blue",
	})
add_row(window.child.lines, 0, "_Dashing", {
	   "Solid", "Dashed", "Dotted",
	})
add_row(window.child.lines, 1, "_Line ends", {
	   "Square", "Round", "Arrow",
	})

function window.child.enable_grouping:on_toggled()
   size_group.mode = self.active and 'HORIZONTAL' or 'NONE'
end

window:show_all()
return window
end,

"Size Groups",

table.concat {
   [[Gtk.SizeGroup provides a mechanism for grouping a number of widgets ]],
   [[together so they all request the same amount of space. This is ]],
   [[typically useful when you want a column of widgets to have the same ]],
   [[size, but you can't use a Gtk.Grid widget.
]],
   [[Note that size groups only affect the amount of space requested, ]],
   [[not the size that the widgets finally receive. If you want ]],
   [[the widgets in a Gtk.SizeGroup to actually be the same size, ]],
   [[you need to pack them in such a way that they get the size they ]],
   [[request and not more. For example, if you are packing your widgets ]],
   [[into a grid, you would not include the 'FILL' flag.]],
}
