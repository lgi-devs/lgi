return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Window {
   title = "Panes",
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.Paned {
	 orientation = 'VERTICAL',
	 Gtk.Paned {
	    id = 'paned_top',
	    orientation = 'HORIZONTAL',
	    Gtk.Frame {
	       id = 'paned_left',
	       shadow_type = 'IN',
	       height_request = 60,
	       width_request = 60,
	       Gtk.Button {
		  label = "_Hi there",
		  use_underline = true,
	       },
	    },
	    Gtk.Frame {
	       id = 'paned_right',
	       shadow_type = 'IN',
	       height_request = 80,
	       width_request = 60,
	    },
	 },
	 Gtk.Frame {
	    id = 'paned_bottom',
	    shadow_type = 'IN',
	    height_request = 60,
	    width_request = 80,
	 },
      },
      Gtk.Frame {
	 label = "Horizontal",
	 border_width = 4,
	 Gtk.Grid {
	    {
	       left_attach = 0, top_attach = 0,
	       Gtk.Label { label = "Left" },
	    },
	    {
	       left_attach = 0, top_attach = 1,
	       Gtk.CheckButton {
		  id = 'resize_left',
		  label = "_Resize",
		  use_underline = true,
	       },
	    },
	    {
	       left_attach = 0, top_attach = 2,
	       Gtk.CheckButton {
		  id = 'shrink_left',
		  label = "_Shrink",
		  use_underline = true,
	       },
	    },
	    {
	       left_attach = 1, top_attach = 0,
	       Gtk.Label { label = "Right" },
	    },
	    {
	       left_attach = 1, top_attach = 1,
	       Gtk.CheckButton {
		  id = 'resize_right',
		  label = "_Resize",
		  use_underline = true,
	       },
	    },
	    {
	       left_attach = 1, top_attach = 2,
	       Gtk.CheckButton {
		  id = 'shrink_right',
		  label = "_Shrink",
		  use_underline = true,
	       },
	    },
	 },
      },
      Gtk.Frame {
	 label = "Vertical",
	 border_width = 4,
	 Gtk.Grid {
	    {
	       left_attach = 0, top_attach = 0,
	       Gtk.Label { label = "Top" },
	    },
	    {
	       left_attach = 0, top_attach = 1,
	       Gtk.CheckButton {
		  id = 'resize_top',
		  label = "_Resize",
		  use_underline = true,
	       },
	    },
	    {
	       left_attach = 0, top_attach = 2,
	       Gtk.CheckButton {
		  id = 'shrink_top',
		  label = "_Shrink",
		  use_underline = true,
	       },
	    },
	    {
	       left_attach = 1, top_attach = 0,
	       Gtk.Label { label = "Bottom" },
	    },
	    {
	       left_attach = 1, top_attach = 1,
	       Gtk.CheckButton {
		  id = 'resize_bottom',
		  label = "_Resize",
		  use_underline = true,
	       },
	    },
	    {
	       left_attach = 1, top_attach = 2,
	       Gtk.CheckButton {
		  id = 'shrink_bottom',
		  label = "_Shrink",
		  use_underline = true,
	       },
	    },
	 },
      },
   },
}

-- Connect servicing routines for all toggles.
for _, pos in ipairs { 'left', 'right', 'top', 'bottom' } do
   local child = window.child['paned_' .. pos]
   local paned = child.parent

   local resize = window.child['resize_' .. pos]
   resize.active = paned.property[child].resize
   function resize:on_clicked()
      paned.property[child].resize = self.active
   end

   local shrink = window.child['shrink_' .. pos]
   shrink.active = paned.property[child].shrink
   function shrink:on_clicked()
      paned.property[child].shrink = self.active
   end
end

window:show_all()
return window
end,

"Paned Widgets",

table.concat {
   [[The Gtk.Paned widget divide its content area into two panes ]],
   [[with a divider in between that the user can adjust. A separate ]],
   [[child is placed into each pane.
]],
   [[There are a number of options that can be set for each pane. ]],
   [[This test contains both a horizontal and a vertical widget, ]],
   [[and allows you to adjust the options for each side of each widget.]],
}
