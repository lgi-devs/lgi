return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local Column = {
   FIXED = 1,
   NUMBER = 2,
   SEVERITY = 3,
   DESCRIPTION = 4,
   PULSE = 5,
   ICON = 6,
   ACTIVE = 7,
   SENSITIVE = 8,
}

-- Create the list store.
local store = Gtk.ListStore.new {
   [Column.FIXED] = GObject.Type.BOOLEAN,
   [Column.NUMBER] = GObject.Type.UINT,
   [Column.SEVERITY] = GObject.Type.STRING,
   [Column.DESCRIPTION] = GObject.Type.STRING,
   [Column.PULSE] = GObject.Type.UINT,
   [Column.ICON] = GObject.Type.STRING,
   [Column.ACTIVE] = GObject.Type.BOOLEAN,
   [Column.SENSITIVE] = GObject.Type.BOOLEAN,
}

-- Populate it with sample data.
for i, item in ipairs {
   { false, 60482, "Normal",     "scrollable notebooks and hidden tabs" },
   { false, 60620, "Critical",   "gdk_window_clear_area (gdkwindow-win32.c) is not thread-safe" },
   { false, 50214, "Major",      "Xft support does not clean up correctly" },
   { true,  52877, "Major",      "GtkFileSelection needs a refresh method. " },
   { false, 56070, "Normal",     "Can't click button after setting in sensitive" },
   { true,  56355, "Normal",     "GtkLabel - Not all changes propagate correctly" },
   { false, 50055, "Normal",     "Rework width/height computations for TreeView" },
   { false, 58278, "Normal",     "gtk_dialog_set_response_sensitive () doesn't work" },
   { false, 55767, "Normal",     "Getters for all setters" },
   { false, 56925, "Normal",     "Gtkcalender size" },
   { false, 56221, "Normal",     "Selectable label needs right-click copy menu" },
   { true,  50939, "Normal",     "Add shift clicking to GtkTextView" },
   { false, 6112,  "Enhancement","netscape-like collapsable toolbars" },
   { false, 1,     "Normal",     "First bug :=)" },
} do
   if i == 2 or i == 4 then
      item[Column.ICON] = 'battery-caution-charging-symbolic'
   end
   item[Column.SENSITIVE] = (i ~= 4)
   store:append(item)
end

local window = Gtk.Window {
   title = "Gtk.ListStore demo",
   default_width = 280,
   default_height = 250,
   border_width = 8,
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 8,
      Gtk.Label {
	 label = "This is the bug list (note: not based on real data, "
	    .. "it would be nice to have a nice ODBC interface to bugzilla "
	    .. "or so, though"
      },
      Gtk.ScrolledWindow {
	 shadow_type = 'ETCHED_IN',
	 hscrollbar_policy = 'NEVER',
	 expand = true,
	 Gtk.TreeView {
	    id = 'view',
	    model = store,
	    Gtk.TreeViewColumn {
	       title = "Fixed?",
	       sizing = 'FIXED',
	       fixed_width = 50,
	       {
		  Gtk.CellRendererToggle { id = 'fixed_renderer' },
		  { active = Column.FIXED },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Bug number",
	       sort_column_id = Column.NUMBER - 1,
	       {
		  Gtk.CellRendererText {},
		  { text = Column.NUMBER },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Severity",
	       sort_column_id = Column.SEVERITY - 1,
	       {
		  Gtk.CellRendererText {},
		  { text = Column.SEVERITY },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Description",
	       sort_column_id = Column.DESCRIPTION - 1,
	       {
		  Gtk.CellRendererText {},
		  { text = Column.DESCRIPTION },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Spinning",
	       sort_column_id = Column.PULSE - 1,
	       {
		  Gtk.CellRendererSpinner {},
		  {
		     pulse = Column.PULSE,
		     active = Column.ACTIVE
		  },
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Symbolic icon",
	       sort_column_id = Column.ICON - 1,
	       {
		  Gtk.CellRendererPixbuf { follow_state = true },
		  {
		     icon_name = Column.ICON,
		     sensitive = Column.SENSITIVE,
		  },
	       },
	    },
	 },
      },
   },
}

function window.child.fixed_renderer:on_toggled(path_str)
   local path = Gtk.TreePath.new_from_string(path_str)
   -- Change current value.
   store[path][Column.FIXED] = not store[path][Column.FIXED]
end

-- Add 'animation' for the spinner.
local timer = GLib.timeout_add(
   GLib.PRIORITY_DEFAULT, 80,
   function()
      local row = store[store:get_iter_first()]
      local pulse = row[Column.PULSE]
      pulse = (pulse > 32768) and 0 or pulse + 1
      row[Column.PULSE] = pulse
      row[Column.ACTIVE] = true
      return true
   end)

function window:on_destroy()
   GLib.source_remove(timer)
end

window:show_all()
return window
end,

"Tree View/List Store",

table.concat {
   [[The Gtk.ListStore is used to store data in list form, to be used later ]],
   [[on by a Gtk.TreeView to display it. This demo builds a simple ]],
   [[Gtk.ListStore and displays it. See the Stock Browser demo for a more ]],
   [[advanced example.]],
}
