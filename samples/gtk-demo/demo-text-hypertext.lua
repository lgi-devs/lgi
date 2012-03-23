return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local Pango = lgi.Pango

local window = Gtk.Window {
   title = "Hypertext",
   default_width = 450,
   default_height = 450,
   Gtk.ScrolledWindow {
      Gtk.TextView {
	 id = 'textview',
	 wrap_mode = 'WORD',
      },
   },
}

-- Sample hypertext content.
local content = {
   intro = [[
Some text to show that simple [hypertext hypertext] can easily be realized with [tags tags].
]],
   hypertext = [[
*hypertext:*
machine-readable text that is not sequential but is organized so that related items of information are connected.
[intro Go back]
]],
   tags = [[
A tag is an attribute that can be applied to some range of text. For example, a tag might be called "bold" and make the text inside the tag bold. However, the tag concept is more general than that; tags don't have to affect appearance. They can instead affect the behavior of mouse and key presses, "lock" a range of text so the user can't edit it, or countless other things.
[intro Go back]
]],
}

local active_links
local handlers = {
   ['^([^%[%*]+)'] =
      -- Plaintext.
      function(text)
	 return text
      end,
   ['^%[(%w+) ([^%]]+)%]'] =
      -- Link.
      function(link, text)
	 local tag = Gtk.TextTag {
	    foreground = 'blue',
	    underline = Pango.Underline.SINGLE,
	 }
	 active_links[tag] = link
	 return text, tag
      end,
   ['^%*([^%*]+)%*'] =
      -- Bold text.
      function(text)
	 return text, Gtk.TextTag {
	    weight = Pango.Weight.BOLD,
	 }
      end,
}

local function fill_page(page)
   local buffer = window.child.textview.buffer
   buffer.text = ''
   active_links = {}
   if not page then return end
   local iter = buffer:get_iter_at_offset(0)
   local pos = 1
   repeat
      for pattern, handler in pairs(handlers) do
	 local start, stop, m1, m2 = page:find(pattern, pos)
	 if start then
	    -- Extract next part of the text.
	    local text, tag = handler(m1, m2)

	    -- Add text into the buffer.
	    start = iter:get_offset()
	    buffer:insert(iter, text, -1)

	    -- Apply tag, if available.
	    if tag then
	       buffer.tag_table:add(tag)
	       buffer:apply_tag(tag, buffer:get_iter_at_offset(start), iter)
	    end

	    -- Prepare for the next iteration.
	    pos = stop + 1
	    break
	 end
      end
   until pos >= #page
end

local cursors = {
   [true] = Gdk.Cursor.new('HAND2'),
   [false] = Gdk.Cursor.new('XTERM'),
}

local hovering = false
local function set_cursor_if_appropriate(view, x, y)
   local tags = view:get_iter_at_location(x, y):get_tags()
   local should_hover = false
   for i = 1, #tags do
      if active_links[tags[i]] then
	 should_hover = true
	 break
      end
   end
   if hovering ~= should_hover then
      hovering = should_hover
      view:get_window('TEXT'):set_cursor(cursors[hovering])
   end
end

local textview = window.child.textview
function textview:on_motion_notify_event(event)
   set_cursor_if_appropriate(
      self, self:window_to_buffer_coords('WIDGET', event.x, event.y))
   return false
end

function textview:on_visibility_notify_event(event)
   local x, y = self.window:get_pointer()
   if x and y then
      set_cursor_if_appropriate(
	 self, self:window_to_buffer_coords('WIDGET', x, y))
   end
   return false
end

local function follow_if_link(view, iter)
   for _, tag in ipairs(iter:get_tags()) do
      if active_links[tag] then
	 fill_page(content[active_links[tag]])
	 break
      end
   end
end

function textview:on_event_after(event)
   if event.type == 'BUTTON_RELEASE' and event.button.button == 1 then
      -- Don't follow link if anything is selected.
      local start, stop = self.buffer:get_selection_bounds()
      if not start or not stop or start:get_offset() == stop:get_offset() then
	 follow_if_link(self, self:get_iter_at_location(
			   self:window_to_buffer_coords(
			      'WIDGET', event.button.x, event.button.y)))
      end
   end
end

function textview:on_key_press_event(event)
   if event.keyval == Gdk.KEY_Return or event.keyval == Gdk.KEY_KP_Enter then
      follow_if_link(
	 self, self.buffer:get_iter_at_mark(self.buffer:get_insert()))
   end
   return false
end

-- Initially fill the intro page.
fill_page(content.intro)

window:show_all()
return window
end,

"Text Widget/Hypertext",

table.concat {
   [[Usually, tags modify the appearance of text in the view, ]],
   [[e.g. making it bold or colored or underlined. But tags are not ]],
   [[restricted to appearance. They can also affect the behavior of ]],
   [[mouse and key presses, as this demo shows.]],
}
