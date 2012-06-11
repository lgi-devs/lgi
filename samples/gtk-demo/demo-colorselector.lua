return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local cairo = lgi.cairo

local window = Gtk.Window {
   title = "Color Selection",
   border_width = 8,
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 8,
      border_width = 8,
      Gtk.Frame {
	 shadow_type = 'IN',
	 Gtk.DrawingArea {
	    id = 'area',
	    expand = true,
	    width = 200,
	    height = 200,
	 },
      },
      Gtk.Button {
	 id = 'change',
	 label = "_Change the above color",
	 use_underline = true,
	 halign = 'END',
	 valign = 'CENTER',
      },
   },
}

local area = window.child.area
area:override_background_color(
   0, Gdk.RGBA { red = 0, green = 0, blue = 1, alpha = 1 })

function area:on_draw(cr)
   cr:set_source_rgba(self.style_context:get_background_color('NORMAL'))
   cr:paint()
   return true
end

function window.child.change:on_clicked()
   local dialog
   if Gtk.ColorChooserDialog then
      dialog = Gtk.ColorChooserDialog {
	 title = "Changing color",
	 transient_for = window,
	 rgba = self.style_context:get_background_color('NORMAL')
      }
      function dialog:on_response(response_id)
	 if response_id == Gtk.ResponseType.OK then
	    area:override_background_color(0, self.rgba)
	 end
	 dialog:hide()
      end
   else
      dialog = Gtk.ColorSelectionDialog {
	 title = "Changing color",
	 transient_for = window,
      }
      dialog.color_selection.current_rgba =
	 self.style_context:get_background_color('NORMAL')
      function dialog:on_response(response_id)
	 if response_id == Gtk.ResponseType.OK then
	    area:override_background_color(
	       0, self.color_selection.current_rgba)
	 end
	 dialog:hide()
      end
   end
   dialog:show_all()
end

window:show_all()
return window
end,

"Color Selector",

table.concat {
   [[Gtk.ColorSelection lets the user choose a color. ]],
   [[Gtk.ColorSelectionDialog is a prebuilt dialog containing ]],
   [[a Gtk.ColorSelection.]],
}
