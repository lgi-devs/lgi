return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gio = lgi.Gio
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local cairo = lgi.cairo
local Pango = lgi.Pango
local PangoCairo = lgi.PangoCairo

local assert = lgi.assert

-- Prepare settings.
local settings = Gtk.PrintSettings {}
local outdir = GLib.get_user_special_dir('DIRECTORY_DOCUMENTS')
   or GLib.get_home_dir()
settings:set(Gtk.PRINT_OUTPUT_URI, 'file://' .. outdir .. '/gtk-demo.'
	     .. (settings:get(Gtk.PRINT_OUTPUT_FILE_FORMAT) or 'pdf'))

-- Create the print operation.
local operation = Gtk.PrintOperation {
   use_full_page = true,
   unit = 'POINTS',
   embed_page_setup = true,
   print_settings = settings
}

local HEADER_HEIGHT = 10 * 72 / 25.4
local HEADER_GAP = 3 * 72 / 25.4
local font_size = 12
local contents

function operation:on_begin_print(context)
   contents = {}

   local height = context:get_height() - HEADER_HEIGHT - HEADER_GAP
   contents.lines_per_page = math.floor(height / font_size)

   -- Parse input stream into lines.
   local file = dir:get_child('demo-printing.lua')
   contents.filename = file:query_info(
      Gio.FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME, 'NONE'):get_display_name()
   local input = Gio.DataInputStream {
      newline_type = 'ANY',
      base_stream = assert(file:read()),
   }
   while true do
      local line, len = input:read_line_utf8()
      if not line and len == 0 then break end
      assert(line, len)
      contents[#contents + 1] = line
   end
   contents.num_pages =
      math.floor((#contents - 1) / contents.lines_per_page + 1)
   self:set_n_pages(contents.num_pages)
end

function operation:on_draw_page(context, page_nr)
   local cr = context:get_cairo_context()
   local width = context:get_width()

   cr:rectangle(0, 0, width, HEADER_HEIGHT)
   cr:set_source_rgb(0.8, 0.8, 0.8)
   cr:fill_preserve()

   cr:set_source_rgb(0, 0, 0)
   cr.line_width = 1
   cr:stroke()

   local layout = context:create_pango_layout()
   layout.font_description = Pango.FontDescription.from_string('sans 14')
   layout.text = contents.filename
   local text_width, text_height = layout:get_pixel_size()
   if text_width > width then
      layout.width = width
      layout.ellipsize = 'START'
      text_width, text_height = layout:get_pixel_size()
   end

   cr:move_to((width - text_width) / 2, (HEADER_HEIGHT - text_height) / 2)
   cr:show_layout(layout)

   layout.text = ("%d/%d"):format(page_nr + 1, contents.num_pages)
   layout.width = -1
   text_width, text_height = layout:get_pixel_size()
   cr:move_to(width - text_width - 4, (HEADER_HEIGHT - text_height) / 2)
   cr:show_layout(layout)

   layout = context:create_pango_layout()
   layout.font_description = Pango.FontDescription.from_string('monospace')

   cr:move_to(0, HEADER_HEIGHT + HEADER_GAP)
   local line = page_nr * contents.lines_per_page
   for i = 1, math.min(#contents - line, contents.lines_per_page) do
      layout.text = contents[line + i]
      cr:show_layout(layout)
      cr:rel_move_to(0, font_size)
   end
end

-- Run the operation
local ok, err = operation:run('PRINT_DIALOG', parent)
if not ok then
   local dialog = Gtk.MessageDialog {
      transient_for = parent,
      destroy_with_parent = true,
      message_type = 'ERROR',
      buttons = 'CLOSE',
      message = err,
      on_response = Gtk.Widget.destroy,
   }
   dialog:show_all()
end

end,

"Printing",

table.concat {
   [[Gtk.PrintOperation offers a simple API to support printing ]],
   [[in a cross-platform way.]],
}
