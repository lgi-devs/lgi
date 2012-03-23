return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Dialog {
   title = "Gtk.Spinner",
   transient_for = parent,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.NONE },
   },
   resizable = false,
   on_response = Gtk.Widget.destroy,
}

window:get_content_area():add(
   Gtk.Box {
      orientation = 'VERTICAL',
      border_width = 5,
      spacing = 5,
      Gtk.Box {
	 orientation = 'HORIZONTAL',
	 spacing = 5,
	 Gtk.Spinner {
	    id = 'sensitive',
	    active = true,
	 },
	 Gtk.Entry {},
      },
      Gtk.Box {
	 orientation = 'HORIZONTAL',
	 spacing = 5,
	 Gtk.Spinner {
	    id = 'insensitive',
	    sensitive = false,
	    active = true,
	 },
	 Gtk.Entry {},
      },
      Gtk.Button {
	 id = 'play',
	 label = Gtk.STOCK_MEDIA_PLAY,
	 use_stock = true,
      },
      Gtk.Button {
	 id = 'stop',
	 label = Gtk.STOCK_MEDIA_STOP,
	 use_stock = true,
      },
   })

function window.child.play:on_clicked()
   window.child.sensitive.active = true
   window.child.insensitive.active = true
end

function window.child.stop:on_clicked()
   window.child.sensitive.active = false
   window.child.insensitive.active = false
end

window:show_all()
return window
end,

"Spinner",

table.concat {
   [[Gtk.Spinner allows to show that background activity is on-going.]]
}
