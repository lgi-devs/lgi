return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk

local log = lgi.log.domain 'gtk-demo'

local function get_image_pixbuf(image)
   if image.storage_type == 'PIXBUF' then
      return image.pixbuf
   elseif image.storage_type == 'STOCK' then
      return image:render_icon_pixbuf(image.stock, image.icon_size)
   else
      log.warning(('Image storage type "%s" not handled'):format(
		     image.storage_type))
   end
end

local function drag_begin(ebox, context)
   Gtk.drag_set_icon_pixbuf(context, get_image_pixbuf(ebox:get_child()),
			    -2, -2)
end

local function drag_data_get(ebox, context, selection_data)
   selection_data:set_pixbuf(get_image_pixbuf(ebox:get_child()))
end

local function drag_data_received(ebox, context, x, y, selection_data)
   if selection_data:get_length() > 0 then
      ebox:get_child().pixbuf = selection_data:get_pixbuf()
   end
end

local function button_press(ebox, event_button)
   if event_button.button ~= 3 then return false end

   local clipboard = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
   local image = ebox:get_child()
   local menu = Gtk.Menu {
      Gtk.ImageMenuItem {
	 use_stock = true, label = Gtk.STOCK_COPY,
	 on_activate = function()
	    local pixbuf = get_image_pixbuf(image)
	    clipboard:set_image(pixbuf)
	 end
      },
      Gtk.ImageMenuItem {
	 use_stock = true, label = Gtk.STOCK_PASTE,
	 on_activate = function()
	    local pixbuf = clipboard:wait_for_image()
	    if pixbuf then image.pixbuf = pixbuf end
	 end
      },
   }
   menu:show_all()
   menu:popup(nil, nil, nil, event_button.button, event_button.time)
end

local window = Gtk.Window {
   title = "Clipboard demo",
   Gtk.Box {
      orientation = 'VERTICAL',
      border_width = 8,
      Gtk.Label { label = "\"Copy\" will copy the text\n" ..
		  "in the entry to the clipboard" },
      Gtk.Box {
	 orientation = 'HORIZONTAL',
	 spacing = 4,
	 border_width = 8,
	 Gtk.Entry { id = 'copy_entry', hexpand = true },
	 Gtk.Button {
	    id = 'copy_button', use_stock = true, label = Gtk.STOCK_COPY
	 },
      },
      Gtk.Label { label = "\"Paste\" will paste the text from " ..
		  "the clipboard to the entry" },
      Gtk.Box {
	 orientation = 'HORIZONTAL',
	 spacing = 4,
	 border_width = 8,
	 Gtk.Entry { id = 'paste_entry', hexpand = true },
	 Gtk.Button {
	    id = 'paste_button', use_stock = true, label = Gtk.STOCK_PASTE
	 },
      },
      Gtk.Label { label = "Images can be transferred via the clipboard, too" },
      Gtk.Box {
	 orientation = 'HORIZONTAL',
	 spacing = 4,
	 border_width = 8,
	 Gtk.EventBox {
	    id = 'ebox1',
	    on_drag_begin = drag_begin,
	    on_drag_data_get = drag_data_get,
	    on_drag_data_received = drag_data_received,
	    on_button_press_event = button_press,
	    Gtk.Image { stock = Gtk.STOCK_DIALOG_WARNING,
			icon_size = Gtk.IconSize.BUTTON }
	 },
	 Gtk.EventBox {
	    id = 'ebox2',
	    on_drag_begin = drag_begin,
	    on_drag_data_get = drag_data_get,
	    on_drag_data_received = drag_data_received,
	    on_button_press_event = button_press,
	    Gtk.Image { stock = Gtk.STOCK_STOP,
			icon_size = Gtk.IconSize.BUTTON }
	 },
      },
   }
}

function window.child.copy_button:on_clicked()
   local entry = window.child.copy_entry
   local clipboard = entry:get_clipboard(Gdk.SELECTION_CLIPBOARD)
   clipboard:set_text(entry.text, -1)
end

function window.child.paste_button:on_clicked()
   local entry = window.child.paste_entry
   local clipboard = entry:get_clipboard(Gdk.SELECTION_CLIPBOARD)
   clipboard:request_text(function(clipboard, text)
			     if text then entry.text = text end
			  end)
end

-- Make eboxes drag sources and destinations.
for _, ebox in ipairs { window.child.ebox1, window.child.ebox2 } do
   ebox:drag_source_set('BUTTON1_MASK', nil, 'COPY')
   ebox:drag_source_add_image_targets()
   ebox:drag_dest_set('ALL', nil, 'COPY')
   ebox:drag_dest_add_image_targets(ebox)
end

-- Tell the clipboard manager to make the data persistent.
Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD):set_can_store(nil)

window:show_all()
return window
end,

"Clipboard",

table.concat {
   "Gtk.Clipboard is used for clipboard handling. This demo shows how to ",
   "copy and paste text to and from the clipboard.\n",
   "It also shows how to transfer images via the clipboard or via ",
   "drag-and-drop, and how to make clipboard contents persist after ",
   "the application exits. Clipboard persistence requires a clipboard ",
   "manager to run."
}
