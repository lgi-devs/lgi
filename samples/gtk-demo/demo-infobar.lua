return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gtk = lgi.Gtk

local window = Gtk.Window {
   title = 'Info Bars',
   border_width = 8,
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.InfoBar {
	 id = 'info',
	 message_type = 'INFO',
      },
      Gtk.InfoBar {
	 id = 'warning',
	 message_type = 'WARNING',
      },
      Gtk.InfoBar {
	 id = 'question',
	 buttons = {
	    { Gtk.STOCK_OK, Gtk.ResponseType.OK },
	 },
	 message_type = 'QUESTION',
      },
      Gtk.InfoBar {
	 id = 'error',
	 message_type = 'ERROR',
      },
      Gtk.InfoBar {
	 id = 'other',
	 message_type = 'OTHER',
      },
      Gtk.Frame {
	 label = "Info Bars",
	 Gtk.Box {
	    orientation = 'VERTICAL',
	    spacing = 8,
	    border_width = 8,
	    {
	       padding = 8,
	       Gtk.Label {
		  label = "An example of different info bars",
	       },
	    }
	 },
      },
   },
}

-- Create contents for the infobars.
for _, id in ipairs { 'info', 'warning', 'question', 'error', 'other' } do
   window.child[id]:get_content_area():add(
      Gtk.Label {
	 label = ("This is an info bar with "
		  .. "message type Gtk.MessageType.%s"):format(
	    GLib.ascii_strup(id, -1)),
      })
end

function window.child.question:on_response(response_id)
   local dialog = Gtk.MessageDialog {
      transient_for = window,
      modal = true,
      destroy_with_parent = true,
      message_type = 'INFO',
      buttons = 'OK',
      text = "You clicked a button on an info bar",
      secondary_text = ("Your response has id %d"):format(response_id)
   }
   dialog:run()
   dialog:destroy()
end

window:show_all()
return window
end,

"Info bar",

table.concat {
   [[Info bar widgets are used to report important messages to the user.]],
}
