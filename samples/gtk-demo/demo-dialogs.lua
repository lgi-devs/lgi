return function(parent, dir)

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local GdkPixbuf = lgi.GdkPixbuf

local window = Gtk.Window {
   title = "Dialogs",
   border_width = 8,
   Gtk.Frame {
      label = "Dialogs",
      Gtk.Box {
	 orientation = 'VERTICAL',
	 border_width = 8,
	 spacing = 8,
	 Gtk.Box {
	    orientation = 'HORIZONTAL',
	    spacing = 8,
	    Gtk.Button {
	       id = 'message_button',
	       label = "_Message Dialog",
	       use_underline = true,
            },
	 },
	 Gtk.Separator { orientation = 'HORIZONTAL' },
	 Gtk.Box {
	    orientation = 'HORIZONTAL',
	    spacing = 8,
	    Gtk.Box {
	       orientation = 'VERTICAL',
	       Gtk.Button {
		  id = 'interactive_button',
		  label = "_Interactive Dialog",
		  use_underline = true,
	       },
	    },
	    Gtk.Grid {
	       row_spacing = 4,
	       column_spacing = 4,
	       {
		  left_attach = 0, top_attach = 0,
		  Gtk.Label {
		     label = "_Entry 1",
		     use_underline = true,
		  },
	       },
	       {
		  left_attach = 1, top_attach = 0,
		  Gtk.Entry {
		     id = 'entry1',
		  },
	       },
	       {
		  left_attach = 0, top_attach = 1,
		  Gtk.Label {
		     label = "E_ntry 2",
		     use_underline = true,
		  },
	       },
	       {
		  left_attach = 1, top_attach = 1,
		  Gtk.Entry {
		     id = 'entry2',
		  },
	       },
            },
	 },
      },
   },
}

local popup_count = 1
function window.child.message_button:on_clicked()
   local dialog = Gtk.MessageDialog {
      transient_for = window,
      modal = true,
      destroy_with_parent = true,
      message_type = 'INFO',
      buttons = 'OK',
      text = "This message box has been popped up the following\n"
	 .. "number of times:",
      secondary_text = ('%d'):format(popup_count),
   }
   dialog:run()
   dialog:destroy()
   popup_count = popup_count + 1
end

function window.child.interactive_button:on_clicked()
   local dialog = Gtk.Dialog {
      title = "Interactive Dialog",
      transient_for = window,
      modal = true,
      destroy_with_parent = true,
      buttons = {
	 { Gtk.STOCK_OK, Gtk.ResponseType.OK },
	 { "_Non-stock Button", Gtk.ResponseType.CANCEL },
      },
   }
   local hbox = Gtk.Box {
      orientation = 'HORIZONTAL',
      spacing = 8,
      border_width = 8,
      Gtk.Image {
	 stock = Gtk.STOCK_DIALOG_QUESTION,
	 icon_size = Gtk.IconSize.DIALOG,
      },
      Gtk.Grid {
	 row_spacing = 4,
	 column_spacing = 4,
	 {
	    left_attach = 0, top_attach = 0,
	    Gtk.Label {
	       label = "_Entry 1",
	       use_underline = true,
	    },
	 },
	 {
	    left_attach = 1, top_attach = 0,
	    Gtk.Entry {
	       id = 'entry1',
	       text = window.child.entry1.text,
            },
	 },
	 {
	    left_attach = 0, top_attach = 1,
	    Gtk.Label {
	       label = "E_ntry 2",
	       use_underline = true,
	    },
	 },
	 {
	    left_attach = 1, top_attach = 1,
	    Gtk.Entry {
	       id = 'entry2',
	       text = window.child.entry2.text,
            },
	 },
      }
   }
   dialog:get_content_area():add(hbox)
   hbox:show_all()

   if dialog:run() == Gtk.ResponseType.OK then
      window.child.entry1.text = hbox.child.entry1.text
      window.child.entry2.text = hbox.child.entry2.text
   end
   dialog:destroy()
end

window:show_all()
return window
end,

"Dialog and Message Boxes",

table.concat {
   "Dialog widgets are used to pop up a transient window for user feedback.",
}
