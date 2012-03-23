return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local Pango = lgi.Pango
local GdkPixbuf = lgi.GdkPixbuf

-- Create shared text buffer.
local buffer = Gtk.TextBuffer {
   tag_table = Gtk.TextTagTable {
      -- Create a bunch of tags.
      Gtk.TextTag {
	 name = 'heading',
	 weight = Pango.Weight.BOLD,
	 size = 15 * Pango.SCALE,
      },
      Gtk.TextTag {
	 name = 'italic',
	 style = Pango.Style.ITALIC,
      },
      Gtk.TextTag {
	 name = 'bold',
	 weight = Pango.Weight.BOLD,
      },
      Gtk.TextTag {
	 name = 'big',
	 -- points times the Pango.SCALE factor
	 size = 20 * Pango.SCALE,
      },
      Gtk.TextTag {
	 name = 'xx-small',
	 scale = Pango.SCALE_XX_SMALL,
      },
      Gtk.TextTag {
	 name = 'x-large',
	 scale = Pango.SCALE_X_LARGE,
      },
      Gtk.TextTag {
	 name = 'monospace',
	 family = 'monospace',
      },
      Gtk.TextTag {
	 name = 'blue_foreground',
	 foreground = 'blue',
      },
      Gtk.TextTag {
	 name = 'red_background',
	 background = 'red',
      },
      Gtk.TextTag {
	 name = 'big_gap_before_line',
	 pixels_above_lines = 30,
      },
      Gtk.TextTag {
	 name = 'big_gap_after_line',
	 pixels_below_lines = 30,
      },
      Gtk.TextTag {
	 name = 'double_spaced_line',
	 pixels_inside_wrap = 10,
      },
      Gtk.TextTag {
	 name = 'not_editable',
	 editable = false,
      },
      Gtk.TextTag {
	 name = 'word_wrap',
	 wrap_mode = 'WORD',
      },
      Gtk.TextTag {
	 name = 'char_wrap',
	 wrap_mode = 'CHAR',
      },
      Gtk.TextTag {
	 name = 'no_wrap',
	 wrap_mode = 'NONE',
      },
      Gtk.TextTag {
	 name = 'center',
	 justification = 'CENTER',
      },
      Gtk.TextTag {
	 name = 'right_justify',
	 justification = 'RIGHT',
      },
      Gtk.TextTag {
	 name = 'wide_margins',
	 left_margin = 50,
	 right_margin = 50,
      },
      Gtk.TextTag {
	 name = 'strikethrough',
	 strikethrough = true,
      },
      Gtk.TextTag {
	 name = 'underline',
	 underline = 'SINGLE',
      },
      Gtk.TextTag {
	 name = 'double_underline',
	 underline = 'DOUBLE',
      },
      Gtk.TextTag {
	 name = 'superscript',
	 rise = 10 * Pango.SCALE,  -- 10 pixels
	 size = 8 * Pango.SCALE,   -- 8 points
      },
      Gtk.TextTag {
	 name = 'subscript',
	 rise = -10 * Pango.SCALE,  -- 10 pixels
	 size = 8 * Pango.SCALE,   -- 8 points
      },
      Gtk.TextTag {
	 name = 'rtl_quote',
	 wrap_mode = 'WORD',
	 direction = 'RTL',
	 indent = 30,
	 left_margin = 20,
	 right_margin = 20,
      },
   },
}

local pixbuf = GdkPixbuf.Pixbuf.new_from_file(
   dir:get_child('gtk-logo-rgb.gif'):get_path())
pixbuf = pixbuf:scale_simple(32, 32, 'BILINEAR')

local anchors = {}

local iter = buffer:get_iter_at_offset(0)
for _, item in ipairs {
   { [[
The text widget can display text with all kinds of nifty attributes. It also supports multiple views of the same buffer; this demo is showing the same buffer in two places.

]] },
   { [[Font styles. ]], 'heading' },
   { [[For example, you can have ]] },
   { [[italic]], 'italic' },
   { [[, ]] },
   { [[bold]], 'bold' },
   { [[, or ]] },
   { [[monospace (typewriter)]], 'monospace' },
   { [[, or ]] },
   { [[big]], 'big' },
   { [[ text. ]] },
   { [[
It's best not to hardcode specific text sizes; you can use relative sizes as with CSS, such as ]] },
   { [[xx-small]], 'xx-small' },
   { [[ or ]] },
   { [[x-large]], 'x-large' },
   { [[ to ensure that your program properly adapts if the user changes the default font size.

]] },
   { [[Colors. ]], 'heading' },
   { [[Colors such as ]] },
   { [[a blue foreground]], 'blue_foreground' },
   { [[ or ]] },
   { [[a red background]], 'red_background' },
   { [[ or even ]] },
   { [[a blue foreground on red background]],
     'blue_foreground', 'red_background' },
   { [[ (select that to read it) can be used.

]] },
   { [[Underline, strikethrough and rise. ]], 'heading' },
   { [[Strikethrough]], 'strikethrough' },
   { [[, ]] },
   { [[underline]], 'underline' },
   { [[, ]] },
   { [[double underline]], 'double_underline' },
   { [[, ]] },
   { [[superscript]], 'superscript' },
   { [[, and ]] },
   { [[subscript]], 'subscript' },
   { [[ are all supported.

]] },
   { [[Images. ]], 'heading' },
   { [[The buffer can have images in it: ]] },
   { pixbuf }, { pixbuf }, { pixbuf },
   { [[ for example.

]] },
   { [[Spacing. ]], 'heading' },
   { [[You can adjust the amount of space before each line.
]] },
   { [[This line has a whole lot of space before it.
]], 'big_gap_before_line', 'wide_margins' },
   { [[You can also adjust the amount of space after each line; this line has a while lot of space after it
]], 'big_gap_after_line', 'wide_margins' },
   { [[You can also adjust the amount of space between wrapped lines; this line has extra space between each wrapped line in the same paragraph. To show off wrapping some filler text: the quick brown fox jumped over the lazy dog. Blah blah blah blah blah blah blah blah blah.
]], 'double_spaced_line', 'wide_margins' },
   { [[Editability. ]], 'heading' },
   { [[This line is 'locked down' and can't be edited by the user - just try it! You can't delete this line.

]], 'not_editable' },
   { [[Wrapping. ]], 'heading' },
   { [[This line (and most of the others in this buffer) is word-wrapped, using the proper Unicode algorithm. Word wrap should work in all scripts and languages that GTK+ supports. Let's make this a long paragraph to demonstrate: blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah

]] },
   { [[This line has character-based wrapping, and can wrap between any two character glyphs. Let's make this a long paragraph to demonstrate: blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah blah

]], 'char_wrap' },
   { [[This line has all wrapping turned off, so it makes the horizontal scrollbar appear.


]], 'no_wrap' },
   { [[Justification. ]], 'heading' },
   { [[

This line has center justification
]], 'center' },
   { [[This line has right jusitification]], 'right_justify' },
   { [[

This line has big wide margins. Text text text text text text text text text text text text text text text text text text text text text text text text text text text text text text text text text text text
]], 'wide_margins' },
   { [[Internationalization. ]], 'heading' },
   { [[
 You can put all sorts of Unicode text in the buffer.

German (Deutsch Süd) Grüß Gott
Greek (Ελληνικά) Γειά σας
Hebrew      שלום
Japanese (日本語)


The widget properly handles bidirectional text, word wrapping, DOS/UNIX/Unicode paragraph separators, grapheme boundaries, and so on using the Pango internationalization framework.
Here's a word-wrapped quote in a right-to-left language:
]] },
   { [[وقد بدأ ثلاث من أكثر المؤسسات تقدما في شبكة اكسيون برامجها كمنظمات لا تسعى للربح، ثم تحولت في السنوات الخمس الماضية إلى مؤسسات مالية منظمة، وباتت جزءا من النظام المالي في بلدانها، ولكنها تتخصص في خدمة قطاع المشروعات الصغيرة. وأحد أكثر هذه المؤسسات نجاحا هو »بانكوسول« في بوليفيا.

]], 'rtl_quote' },
   { [[You can put widgets in the buffer: Here s a button: ]] },
   function()
      return Gtk.Button {
	 label = "Click Me",
      }
   end,
   { [[ and a menu: ]] },
   function()
      local combo = Gtk.ComboBoxText {}
      combo:append_text("Option 1")
      combo:append_text("Option 2")
      combo:append_text("Option 3")
      return combo
   end,
   { [[ and a scale: ]] },
   function()
      local scale = Gtk.Scale {
	 adjustment = Gtk.Adjustment {
	    lower = 0,
	    upper = 100,
	 }
      }
      scale:set_size_request(70, -1)
      return scale
   end,
   { [[ and an animation: ]] },
   function()
      return Gtk.Image { file = dir:get_child('floppybuddy.gif'):get_path() }
   end,
   { [[ finally a text entry: ]] },
   function()
      return Gtk.Entry()
   end,
   { [[.



This demo does not demonstrate all the Gtk.TextBuffer features; it leaves out, for example: invisible/hidden text, tab stops, application-drawn areas on the sides of the widget for displaying breakpoints and such...]] }
} do
   if type(item) == 'function' then
      anchors[buffer:create_child_anchor(iter)] = item
   elseif type(item[1]) == 'string' then
      local offset = iter:get_offset()
      buffer:insert(iter, item[1], -1)
      for i = 2, #item do
	 buffer:apply_tag_by_name(
	    item[i], buffer:get_iter_at_offset(offset), iter)
      end
   elseif GdkPixbuf.Pixbuf:is_type_of(item[1]) then
      buffer:insert_pixbuf(iter, item[1])
   end
end

-- Apply word_wrap tag to the whole buffer.
buffer:apply_tag_by_name('word_wrap', buffer:get_bounds())

local window = Gtk.Window {
   title = "TextView",
   default_width = 450,
   default_height = 450,
   Gtk.Paned {
      orientation = 'VERTICAL',
      border_width = 5,
      Gtk.ScrolledWindow {
	 Gtk.TextView {
	    id = 'view1',
	    buffer = buffer,
	 },
      },
      Gtk.ScrolledWindow {
	 Gtk.TextView {
	    id = 'view2',
	    buffer = buffer,
	 }
      },
   },
}

-- Create and attach widgets to anchors.
for _, view in pairs { window.child.view1, window.child.view2 } do
   for anchor, creator in pairs(anchors) do
      view:add_child_at_anchor(creator(), anchor)
   end
end

window:show_all()
return window
end,

"Text Widget/Multiple Views",

table.concat {
   [[The Gtk.TextView widget displays a Gtk.TextBuffer. One ]],
   [[Gtk.TextBuffer can be displayed by multiple Gtk.TextViews. ]],
   [[This demo has two views displaying a single buffer, and shows off ]],
   [[the widget's text formatting features.]],
}
