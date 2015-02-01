return function(parent, dir)

local table = require 'table'
local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local GdkPixbuf = lgi.GdkPixbuf

local BrowserColumn = {
   ID = 1,
   LABEL = 2,
   SMALL_ICON = 3,
   ACCEL_STR = 4,
   MACRO = 5,
}

local function create_model()
   local store = Gtk.ListStore.new {
      [BrowserColumn.ID] = GObject.Type.STRING,
      [BrowserColumn.LABEL] = GObject.Type.STRING,
      [BrowserColumn.SMALL_ICON] = GdkPixbuf.Pixbuf,
      [BrowserColumn.ACCEL_STR] = GObject.Type.STRING,
      [BrowserColumn.MACRO] = GObject.Type.STRING,
   }

   local ids = Gtk.stock_list_ids()
   table.sort(ids)
   local icon_width, icon_height = Gtk.IconSize.lookup(Gtk.IconSize.MENU)
   for _, id in ipairs(ids) do
      local item = Gtk.stock_lookup(id)
      local macro = GLib.ascii_strup(id, -1):gsub(
	 '^GTK%-', 'Gtk.STOCK_'):gsub('%-', '_')

      local small_icon
      local icon_set = Gtk.IconFactory.lookup_default(id)
      if icon_set then
	 -- Prefer menu size if it exists, otherwise take the first
	 -- available size.
	 local sizes = icon_set:get_sizes()
	 local size = sizes[0]
	 for i = 1, #sizes do
	    if sizes[i] == Gtk.IconSize.MENU then
	       size = Gtk.IconSize.MENU
	       break
	    end
	 end
	 small_icon = parent:render_icon_pixbuf(id, size)
	 if size ~= Gtk.IconSize.MENU then
	    -- Make the result proper size for thumbnail.
	    small_icon = small_icon:scale_simple(icon_width, icon_height,
						 'BILINEAR')
	 end
      end

      local accel_str
      if item and item.keyval ~= 0 then
	 accel_str = Gtk.accelerator_name(item.keyval, item.modifier)
      end

      store:append {
	 [BrowserColumn.ID] = id,
	 [BrowserColumn.LABEL] = item and item.label,
	 [BrowserColumn.SMALL_ICON] = small_icon,
	 [BrowserColumn.ACCEL_STR] = accel_str,
	 [BrowserColumn.MACRO] = macro,
      }
   end
   return store
end

local window = Gtk.Window {
   title = "Stock Icons and Items",
   default_height = 500,
   border_width = 8,
   Gtk.Box {
      orientation = 'HORIZONTAL',
      spacing = 8,
      Gtk.ScrolledWindow {
	 hscrollbar_policy = 'NEVER',
	 expand = true,
	 Gtk.TreeView {
	    id = 'treeview',
	    model = create_model(),
	    Gtk.TreeViewColumn {
	       title = "Identifier",
	       {
		  Gtk.CellRendererPixbuf {},
		  align = 'start',
		  { stock_id = BrowserColumn.ID },
	       },
	       {
		  Gtk.CellRendererText {},
		  expand = true,
		  align = 'start',
		  { text = BrowserColumn.MACRO },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Label",
	       {
		  Gtk.CellRendererText {},
		  { text = BrowserColumn.LABEL },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Accel",
	       {
		  Gtk.CellRendererText {},
		  { text = BrowserColumn.ACCEL_STR },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "ID",
	       {
		  Gtk.CellRendererText {},
		  { text = BrowserColumn.ID },
	       },
	    },
	 },
      },
      Gtk.Box {
	 orientation = 'VERTICAL',
	 spacing = 8,
	 border_width = 4,
	 Gtk.Label {
	    id = 'type_label',
	 },
	 Gtk.Image {
	    id = 'icon_image',
	 },
	 Gtk.Label {
	    id = 'accel_label',
	    use_underline = true,
	 },
	 Gtk.Label {
	    id = 'macro_label',
	 },
	 Gtk.Label {
	    id = 'id_label',
	 },
      },
   }
}

local display = {
   type_label = window.child.type_label,
   icon_image = window.child.icon_image,
   accel_label = window.child.accel_label,
   macro_label = window.child.macro_label,
   id_label = window.child.id_label,
}

local selection = window.child.treeview:get_selection()
selection.mode = 'BROWSE'
function selection:on_changed()
   local model, iter = self:get_selected()
   local view = self:get_tree_view()
   if model and iter then
      local row = model[iter]
      if row[BrowserColumn.SMALL_ICON] and row[BrowserColumn.LABEL] then
	 display.type_label.label = "Icon and Item"
      elseif row[BrowserColumn.SMALL_ICON] then
	 display.type_label.label = "Icon only"
      elseif row[BrowserColumn.LABEL] then
	 display.type_label.label = "Item Only"
      else
	 display.type_label.label = ''
      end

      display.id_label.label = row[BrowserColumn.ID]
      display.macro_label.label = row[BrowserColumn.MACRO]

      if row[BrowserColumn.LABEL] then
	 display.accel_label.label = ("%s %s"):format(
	    row[BrowserColumn.LABEL], row[BrowserColumn.ACCEL_STR] or '')
      else
	 display.accel_label.label = ''
      end

      if row[BrowserColumn.SMALL_ICON] then
	 -- Find the larget available icon size.
	 local best_size, best_pixels = Gtk.IconSize.INVALID, 0
	 for _, size in ipairs(Gtk.IconFactory.lookup_default(
				  row[BrowserColumn.ID]):get_sizes()) do
	    local width, height = Gtk.IconSize.lookup(size)
	    if width * height > best_pixels then
	       best_pixels = width * height
	       best_size = size
	    end
	 end
	 display.icon_image.stock = row[BrowserColumn.ID]
	 display.icon_image.icon_size = best_size
      else
	 display.icon_image.pixbuf = nil
      end
   else
      display.type_label.label = "No selected item"
      display.macro_label.label  = ''
      display.id_label.label = ''
      display.accel_label.label = ''
      display.icon_image.pixbuf = nil
   end
end

window:show_all()
return window
end,

"Stock Item and Icon Browser",

table.concat {
   [[This source code for this demo doesn't demonstrate anything ]],
   [[particularly useful in applications. The purpose of the "demo" ]],
   [[is just to provide a handy place to browse the available stock ]],
   [[icons and stock items.]]
}
