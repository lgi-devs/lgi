return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gtk = lgi.Gtk

local window = Gtk.Window {
   title = "Automatic scrolling",
   default_width = 600,
   default_height = 400,
   Gtk.Box {
      orientation = 'HORIZONTAL',
      spacing = 6,
      homogeneous = true,
      Gtk.ScrolledWindow {
	 Gtk.TextView {
	    id = 'view1',
	    expand = true,
	 }
      },
      Gtk.ScrolledWindow {
	 Gtk.TextView {
	    id = 'view2',
	    expand = true,
	 }
      },
   },
}

for i = 1, 2 do
   local view = window.child['view' .. i]
   local buffer = view.buffer

   local timer
   if i == 1 then
      -- If we want to scroll to the end, including horizontal
      -- scrolling, then we just create a mark with right gravity at
      -- the end of the buffer. It will stay at the end unless
      -- explicitely moved with Gtk.TextBuffer.move_mark().
      local mark = buffer:create_mark(nil, buffer:get_end_iter(), false)
      local count = 0
      timer = GLib.timeout_add(
	 GLib.PRIORITY_DEFAULT, 50,
	 function()
	    -- Insert to the 'end' mark.
	    buffer:insert(buffer:get_iter_at_mark(mark),
			  '\n' .. (' '):rep(count)
		       .. "Scroll to end scroll to end scroll to end "
		       .."scroll to end ",
		       -1)

	    -- Scroll so that the mark is visible onscreen.
	    view:scroll_mark_onscreen(mark)

	    -- Move to the next column, or if we got too far, scroll
	    -- back to left again.
	    count = (count <= 150) and (count + 1) or 0
	    return true
	 end)
   else
      -- If we want to scroll to the bottom, but not scroll
      -- horizontally, then an end mark won't do the job. Just use
      -- Gtk.TextView.scroll_mark_onscreen() explicitely when
      -- needed.
      local count = 0
      local mark = buffer:create_mark(nil, buffer:get_end_iter(), true)
      timer = GLib.timeout_add(
	 GLib.PRIORITY_DEFAULT, 100,
	 function()
	    -- Insert some text into the buffer.
	    local iter = buffer:get_end_iter()
	    buffer:insert(iter,
			  '\n' .. (' '):rep(count)
		       .. "Scroll to bottom scroll to bottom scroll to bottom "
		       .."scroll to bottom ",
		       -1)

	    -- Move the iterator to the beginning of line, so we don't
	    -- scroll in horizontal direction.
	    iter:set_line_offset(0)

	    -- Place mark at iter.
	    buffer:move_mark(mark, iter)

	    -- Scroll the mark onscreen.
	    view:scroll_mark_onscreen(mark)

	    -- Move to the next column, or if we got too far, scroll
	    -- back to left again.
	    count = (count <= 40) and (count + 1) or 0
	    return true
	 end)
   end

   -- Make sure that the timer is destroyed when the view is destroyed too.
   function view:on_destroy()
      GLib.source_remove(timer)
   end
end

window:show_all()
return window
end,

"Text Widget/Automatic scrolling",

table.concat {
   [[This example demonstrates how to use the gravity of Gtk.TextMarks ]],
   [[to keep a text view scrolled to the bottom when appending text.]]
}
