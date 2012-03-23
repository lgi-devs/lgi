return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Dialog {
   title = "GtkExpander",
   transient_for = parent,
   resizable = false,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.NONE },
   },
   on_response = Gtk.Widget.destroy,
}

local content = Gtk.Box {
   orientation = 'VERTICAL',
   spacing = 5,
   border_width = 5,
   Gtk.Label {
      label = "Expander demo.  Click on the triangle for details.",
   },
   Gtk.Expander {
      label = "Details",
      Gtk.Label {
	 label = "Details can be shown or hidden.",
      }
   },
}
window:get_content_area():add(content)

window:show_all()
return window
end,

"Expander",

table.concat {
   [[Gtk.Expander allows to provide additional content that is ]],
   [[initially hidden. This is also known as "disclosure triangle".]]
}
