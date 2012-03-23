return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Window {
   title = "Pickers",
   border_width = 10,
   Gtk.Grid {
      border_width = 10,
      row_spacing = 3,
      column_spacing = 10,
      {
	 left_attach = 0, top_attach = 0,
	 Gtk.Label {
	    label = "Color:",
	    halign = 'START',
	    valign = 'CENTER',
	    hexpand = true,
	 },
      },
      {
	 left_attach = 1, top_attach = 0,
	 Gtk.ColorButton {},
      },
      {
	 left_attach = 0, top_attach = 1,
	 Gtk.Label {
	    label = "Font:",
	    halign = 'START',
	    valign = 'CENTER',
	    hexpand = true,
	 },
      },
      {
	 left_attach = 1, top_attach = 1,
	 Gtk.FontButton {},
      },
      {
	 left_attach = 0, top_attach = 2,
	 Gtk.Label {
	    label = "File:",
	    halign = 'START',
	    valign = 'CENTER',
	    hexpand = true,
	 },
      },
      {
	 left_attach = 1, top_attach = 2,
	 Gtk.FileChooserButton {
	    title = "Pick a File",
	    action = 'OPEN',
	 },
      },
      {
	 left_attach = 0, top_attach = 3,
	 Gtk.Label {
	    label = "Folder:",
	    halign = 'START',
	    valign = 'CENTER',
	    hexpand = true,
	 },
      },
      {
	 left_attach = 1, top_attach = 3,
	 Gtk.FileChooserButton {
	    title = "Pick a Folder",
	    action = 'SELECT_FOLDER',
	 },
      },
      {
	 left_attach = 0, top_attach = 4,
	 Gtk.Label {
	    label = "Mail:",
	    halign = 'START',
	    valign = 'CENTER',
	    hexpand = true,
	 },
      },
      {
	 left_attach = 1, top_attach = 4,
	 Gtk.AppChooserButton {
	    content_type = 'x-scheme-handler/mailto',
	    show_dialog_item = true,
	 },
      },
   }
}

window:show_all()
return window
end,

"Pickers",

table.concat {
   [[These widgets are mainly intended for use in preference ]],
   [[dialogs. They allow to select colors, fonts, files, directories ]],
   [[and applications.]]
}
