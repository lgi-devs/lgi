return function(parent, dir)

local math = require 'math'

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local GdkPixbuf = lgi.GdkPixbuf

local store = Gtk.ListStore.new { GObject.Type.STRING }
for _, name in ipairs { "Red", "Green", "Blue", "Yellow" } do
   store:append { name }
end

local function edited(cell, path_string, text)
   store[Gtk.TreePath.new_from_string(path_string)][1] = text
end

local function set_cell_color(layout, cell, model, iter)
   local rgba, pixel = Gdk.RGBA(), 0
   local label = model[iter][1]
   if label and rgba:parse(label) then
      pixel =
	 math.floor(rgba.red * 255) * (256 * 256 * 256) +
	 math.floor(rgba.green * 255) * (256 * 256) +
	 math.floor(rgba.blue * 255) * 256
   end
   cell.pixbuf = GdkPixbuf.Pixbuf.new('RGB', false, 8, 24, 24)
   cell.pixbuf:fill(pixel)
end

local window = Gtk.Window {
   title = "Editing and Drag-and-Drop",
   Gtk.IconView {
      id = 'icon_view',
      expand = true,
      model = store,
      selection_mode = 'SINGLE',
      item_orientation = 'HORIZONTAL',
      columns = 2,
      reorderable = true,
      cells = {
	 {
	    align = 'start', expand = true,
	    Gtk.CellRendererPixbuf(),
	    set_cell_color,
	 },
	 {
	    align = 'start', expand = true,
	    Gtk.CellRendererText {
	       editable = true,
	       on_edited = edited,
	    },
	    { text = 1 }
	 },
      },
   },
}

window:show_all()
return window
end,

"Icon View/Editing and Drag-and-Drop",

table.concat {
   [[The Gtk.IconView widget supports Editing and Drag-and-Drop. ]],
   [[This example also demonstrates using the generic Gtk.CellLayout ]],
   [[interface to set up cell renderers in an icon view.]]
}
