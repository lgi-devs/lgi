return function(parent, dir)

local coroutine = require 'coroutine'
local lgi = require 'lgi'
local bytes = require 'bytes'
local GLib = lgi.GLib
local Gio = lgi.Gio
local Gtk = lgi.Gtk
local GdkPixbuf = lgi.GdkPixbuf

local window = Gtk.Window {
   title = 'Images',
   border_width = 8,
   Gtk.Box {
      id = 'vbox',
      orientation = 'VERTICAL',
      spacing = 8,
      border_width = 8,
      Gtk.Label {
	 label = "<u>Image loaded from a file:</u>",
	 use_markup = true,
      },
      Gtk.Frame {
	 shadow_type = 'IN',
	 halign = 'CENTER',
	 valign = 'CENTER',
	 Gtk.Image {
	    file = dir:get_child('gtk-logo-rgb.gif'):get_path(),
	 },
      },
      Gtk.Label {
	 label = "<u>Animation loaded from a file:</u>",
	 use_markup = true,
      },
      Gtk.Frame {
	 shadow_type = 'IN',
	 halign = 'CENTER',
	 valign = 'CENTER',
	 Gtk.Image {
	    file = dir:get_child('floppybuddy.gif'):get_path(),
	 },
      },
      Gtk.Label {
	 label = "<u>Symbolic themed icon</u>",
	 use_markup = true,
      },
      Gtk.Frame {
	 shadow_type = 'IN',
	 halign = 'CENTER',
	 valign = 'CENTER',
	 Gtk.Image {
	    gicon = Gio.ThemedIcon.new_with_default_fallbacks(
	       'battery-caution-charging-symbolic'),
	    icon_size = Gtk.IconSize.DIALOG,
	 },
      },
      Gtk.Label {
	 label = "<u>Progressive image loading</u>",
	 use_markup = true,
      },
      Gtk.Frame {
	 shadow_type = 'IN',
	 halign = 'CENTER',
	 valign = 'CENTER',
	 Gtk.Image {
	    id = 'progressive',
	 },
      },
      Gtk.ToggleButton {
	 id = 'sensitive',
	 label = "_Insensitive",
	 use_underline = true,
      },
   }
}

function window.child.sensitive:on_toggled()
   for _, child in ipairs(window.child.vbox.child) do
      if child ~= self then
	 child.sensitive = not self.active
      end
   end
end

local function do_error(err)
   local dialog = Gtk.MessageDialog {
      transient_for = window,
      destroy_with_parent = true,
      text = "Failure reading image 'alphatest.png'",
      secondary_text = err,
      message_type = 'ERROR',
      buttons = 'CLOSE',
      on_response = Gtk.Widget.destroy,
   }
   dialog:show_all()
end

local abort_load, timer_id
local load_coro = coroutine.create(function()
   while not abort_load do
      local stream, err = dir:get_child('alphatest.png'):read()
      if not stream then
	 do_error(err)
	 abort_load = true
	 break
      end

      -- Create pixbuf loader and register callbacks.
      local loader = GdkPixbuf.PixbufLoader()
      function loader:on_area_prepared()
	 local pixbuf = self:get_pixbuf()
	 pixbuf:fill(0xaaaaaaff)
	 window.child.progressive.pixbuf = pixbuf
      end

      function loader:on_area_updated()
	 -- Let the image know that the pixbuf changed.
	 window.child.progressive:queue_draw()
      end

      while not abort_load do
	 -- Wait for the next timer tick.
	 coroutine.yield(true)

	 -- Load a chunk from the stream.
	 local buffer = bytes.new(256)
	 local read, err = stream:read(buffer)
	 if read < 0 then
	    do_error(err)
	    abort_load = true
	 end
	 if read <= 0 then break end
      
	 -- Send it to the pixbuf loader.
	 if not loader:write(tostring(buffer):sub(1, read)) then
	    do_error(err)
	    abort_load = true
	 end
      end
      loader:close()
   end

   -- Make sure that timeout is unregistered when the coroutine does
   -- not run any more.
   timer_id = nil
   return false
end)
timer_id = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 150, load_coro)

-- Stop loading when the window is destroyed.
function window:on_destroy()
   abort_load = true
   if timer_id then
      GLib.source_remove(timer_id)
      coroutine.resume(load_coro)
   end
end

window:show_all()
return window
end,

"Images",

table.concat {
   [[Gtk.Image is used to display an image; the image can be in ]],
   [[a number of formats. Typically, you load an image into a Gdk.Pixbuf, ]],
   [[then display the pixbuf.
     This demo code shows some of the more obscure cases, in the simple ]],
   [[case a call to Gtk.Image.new_from_file() is all you need.]],
}
