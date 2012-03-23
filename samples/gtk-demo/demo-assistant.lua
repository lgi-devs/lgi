return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local Gtk = lgi.Gtk

local assistant = Gtk.Assistant {
   default_height = 300,
   { 
      title = "Page 1", type = 'INTRO',
      Gtk.Grid {
	 id = 'page1',
	 orientation = 'HORIZONTAL',
	 Gtk.Label { label = "You must fill out this entry to continue:" },
	 Gtk.Entry {
	    vexpand = true,
	    id = 'entry',
	    activates_default = true,
	 },
      },
   },
   {
      title = "Page 2", complete = true,
      Gtk.Grid {
	 orientation = 'VERTICAL',
	 Gtk.CheckButton { label = "This is optional data, you may continue " ..
			   "even if you do not check this" }
      },
   },
   {
      title = "Confirmation", type = 'CONFIRM', complete = true,
      Gtk.Label { label = "This is a confirmation page, press 'Apply' " ..
		  "to apply changes" },
   },
   {
      title = "Applying changes", type = 'PROGRESS',
      Gtk.ProgressBar { id = 'progressbar',
			halign = 'CENTER', valign = 'CENTER' },
   },
}

function assistant.child.entry:on_changed()
   assistant.property.page1.complete = (self.text ~= '')
end

function assistant:on_cancel() self:destroy() end
function assistant:on_close() self:destroy() end

function assistant:on_prepare()
   self.title = ("Sample assistant (%d of %d)"):format(
      self:get_current_page() + 1, self:get_n_pages())

   if self:get_current_page() == 3 then
      -- The changes are permanent and cannot be revisited.
      assistant:commit()
   end
end

local progressbar = assistant.child.progressbar
function assistant:on_apply()
   GLib.timeout_add(GLib.PRIORITY_DEFAULT, 100,
		    function()
		       -- Simulate work.
		       local fraction = progressbar.fraction + 0.05
		       if fraction < 1 then
			  progressbar.fraction = fraction
			  return true
		       else
			  assistant:destroy()
			  return false
		       end
		    end)
end

assistant.child.page1:show_all()

--assistant:set_screen(parent:get_screen())
assistant:show_all()
return assistant
end,

"Assistant",

table.concat {
   "Demonstrates a sample multi-step assistant.\n",
   "Assistants are used to divide an operation into several simpler ",
   "sequential steps, and to guide the user through these steps."
}
