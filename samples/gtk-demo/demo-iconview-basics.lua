return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gio = lgi.Gio
local Gtk = lgi.Gtk
local GdkPixbuf = lgi.GdkPixbuf

local assert = lgi.assert

local pixbuf = {
   REGULAR = assert(GdkPixbuf.Pixbuf.new_from_file(
		       dir:get_child('gnome-fs-regular.png'):get_path())),
   DIRECTORY = assert(GdkPixbuf.Pixbuf.new_from_file(
			 dir:get_child('gnome-fs-directory.png'):get_path())),
}

local ViewColumn = {
   PATH = 1,
   DISPLAY_NAME = 2,
   PIXBUF = 3,
   IS_DIRECTORY = 4,
}

local store = Gtk.ListStore.new {
   [ViewColumn.PATH] = Gio.File,
   [ViewColumn.DISPLAY_NAME] = GObject.Type.STRING,
   [ViewColumn.PIXBUF] = GdkPixbuf.Pixbuf,
   [ViewColumn.IS_DIRECTORY] = GObject.Type.BOOLEAN,
}
store:set_default_sort_func(
   function(model, a, b)
      -- Sort folders before files.
      a = model[a]
      b = model[b]
      local is_dir_a, is_dir_b = 
	 a[ViewColumn.IS_DIRECTORY], b[ViewColumn.IS_DIRECTORY]
      if not is_dir_a and is_dir_b then
	 return 1
      elseif is_dir_a and not is_dir_b then
	 return -1
      else
	 return GLib.utf8_collate(a[ViewColumn.DISPLAY_NAME],
				  b[ViewColumn.DISPLAY_NAME])
      end
   end)
store:set_sort_column_id(Gtk.TreeSortable.DEFAULT_SORT_COLUMN_ID,
			 'ASCENDING')

local window = Gtk.Window {
   default_width = 650,
   default_height = 400,
   title = "Gtk.IconView demo",
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.Toolbar {
	 Gtk.ToolButton {
	    id = 'up_button',
	    stock_id = Gtk.STOCK_GO_UP,
	    is_important = true,
	    sensitive = false,
	 },
	 Gtk.ToolButton {
	    id = 'home_button',
	    stock_id = Gtk.STOCK_HOME,
	    is_important = true,
	 },
      },
      Gtk.ScrolledWindow {
	 shadow_type = 'ETCHED_IN',
	 Gtk.IconView {
	    id = 'icon_view',
	    expand = true,
	    selection_mode = 'MULTIPLE',
	    model = store,
	    text_column = ViewColumn.DISPLAY_NAME - 1,
	    pixbuf_column = ViewColumn.PIXBUF - 1,
	    has_focus = true,
	 },
      },
   },
}

local current_dir = Gio.File.new_for_path('/')
local function fill_store()
   store:clear()
   local enum = current_dir:enumerate_children('standard::*', 'NONE')
   while true do
      local info, err = enum:next_file()
      if not info then assert(not err, err) break end
      store:append {
	 [ViewColumn.PATH] = current_dir:get_child(info:get_name()),
	 [ViewColumn.DISPLAY_NAME] = info:get_display_name(),
	 [ViewColumn.IS_DIRECTORY] = info:get_file_type() == 'DIRECTORY',
	 [ViewColumn.PIXBUF] = pixbuf[info:get_file_type()],
      }
   end
   window.child.up_button.sensitive = (current_dir:get_parent() ~= nil)
end
fill_store()

function window.child.up_button:on_clicked()
   current_dir = current_dir:get_parent()
   fill_store()
end

function window.child.home_button:on_clicked()
   current_dir = Gio.File.new_for_path(GLib.get_home_dir())
   fill_store()
end

function window.child.icon_view:on_item_activated(path)
   local row = store[path]
   if row[ViewColumn.IS_DIRECTORY] then
      current_dir = row[ViewColumn.PATH]
      fill_store()
   end
end

window:show_all()
return window
end,

"Icon View/Icon View Basics",

table.concat {
   [[The Gtk.IconView widget is used to display and manipulate icons. ]],
   [[It uses a Gtk.TreeModel for data storage, so the list store example ]],
   [[might be helpful.]]
}
