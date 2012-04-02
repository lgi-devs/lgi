return function(parent, dir)

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local window = Gtk.Dialog {
   title = "GtkEntryCompletion",
   resizable = false,
   on_response = Gtk.Widget.destroy,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.NONE },
   },
}

local store = Gtk.ListStore.new { GObject.Type.STRING }
for _, name in ipairs { "GNOME", "total", "totally" } do
   store:append { name }
end

local content = Gtk.Box {
   orientation = 'VERTICAL',
   spacing = 5,
   border_width = 5,
   Gtk.Label {
      label = "Completion demo, try writing <b>total</b> or <b>gnome</b> "
	 .. "for example.",
      use_markup = true,
   },
   Gtk.Entry {
      id = 'entry',
      completion = Gtk.EntryCompletion {
	 model = store,
	 text_column = 0,
      },
   },
}
window:get_content_area():add(content)

window:show_all()
return window
end,

"Entry/Entry Completion",

table.concat {
   "Gtk.EntryCompletion provides a mechanism for adding support for ",
   "completion in Gtk.Entry.",
}
