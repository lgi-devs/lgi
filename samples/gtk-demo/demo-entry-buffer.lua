return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Dialog {
   title = "Entry Buffer",
   resizable = false,
   on_response = Gtk.Widget.destroy,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.NONE },
   },
}

local buffer = Gtk.EntryBuffer()
local content = Gtk.Box {
   orientation = 'VERTICAL',
   spacing = 5,
   border_width = 5,
   Gtk.Label {
      label = "Entries share a buffer. "
	 .. "Typing in one is reflected in the other.",
      use_markup = true,
   },
   Gtk.Entry {
      buffer = buffer,
   },
   Gtk.Entry {
      buffer = buffer,
      visibility = false,
   },
}
window:get_content_area():add(content)

window:show_all()
return window
end,

"Entry/Entry Buffer",

table.concat {
   "Gtk.EntryBuffer provides the text content in a Gtk.Entry.",
}
