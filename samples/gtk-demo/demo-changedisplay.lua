return function(parent, dir)

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk

-- Create main dialog window
local window = Gtk.Dialog {
   title = "Change Screen or Display",
   transient_for = parent,
   default_width = 300,
   default_height = 400,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.CLOSE },
      { "Change", Gtk.ResponseType.OK },
   },
}

-- Define column names for 'display' model.
local DisplayColumn = {
   NAME = 1,
   DISPLAY = 2,
}

-- Define column numbers for 'screens' model.
local ScreenColumn = {
   NUMBER = 1,
   SCREEN = 2,
}

-- Add content of the main dialog.
window:get_content_area():add(
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 5,
      border_width = 8,
      Gtk.Frame {
	 label = "Display",
	 Gtk.Box {
	    orientation = 'HORIZONTAL',
	    spacing = 8,
	    border_width = 8,
	    Gtk.ScrolledWindow {
	       shadow_type = 'IN',
	       hscrollbar_policy = 'NEVER',
	       expand = true,
	       Gtk.TreeView {
		  id = 'displays',
		  headers_visible = false,
		  model = Gtk.ListStore.new {
		     [DisplayColumn.NAME] = GObject.Type.STRING,
		     [DisplayColumn.DISPLAY] = Gdk.Display,
		  },
		  Gtk.TreeViewColumn {
		     title = "Name",
		     { Gtk.CellRendererText(),
		       { text = DisplayColumn.NAME } },
		  },
	       },
	    },
	    Gtk.Box {
	       id = 'displays_box',
	       orientation = 'VERTICAL',
	       spacing = 5,
	       Gtk.Button {
		  id = 'display_open',
		  label = "_Open...",
		  use_underline = true,
	       },
	       Gtk.Button {
		  id = 'display_close',
		  label = "Close",
		  use_underline = true,
	       },
	    },
	 },
      },
      Gtk.Frame {
	 label = "Screen",
	 Gtk.Box {
	    orientation = 'HORIZONTAL',
	    spacing = 8,
	    border_width = 8,
	    Gtk.ScrolledWindow {
	       shadow_type = 'IN',
	       hscrollbar_policy = 'NEVER',
	       expand = true,
	       Gtk.TreeView {
		  id = 'screens',
		  headers_visible = false,
		  model = Gtk.ListStore.new {
		     [ScreenColumn.NUMBER] = GObject.Type.INT,
		     [ScreenColumn.SCREEN] = Gdk.Screen,
		  },
		  Gtk.TreeViewColumn {
		     title = "Number",
		     { Gtk.CellRendererText(),
		       { text = ScreenColumn.NUMBER } },
		  },
	       },
	    },
	    Gtk.Box {
	       id = 'screens_box',
	       orientation = 'VERTICAL',
	       spacing = 5,
	    },
	 },
      },
   })

local current_display, current_screen

local display_selection = window.child.displays:get_selection()
local screen_selection = window.child.screens:get_selection()

display_selection.mode = 'BROWSE'
function display_selection:on_changed()
   local model, iter = self:get_selected()
   if model then
      current_display = model[iter][DisplayColumn.DISPLAY]
      local screens = window.child.screens.model
      screens:clear()
      for i = 0, current_display:get_n_screens() - 1 do
	 local iter = screens:append {
	    [ScreenColumn.NUMBER] = i,
	    [ScreenColumn.SCREEN] = current_display:get_screen(i),
	 }
	 if i == 0 then
	    screen_selection:select_iter(iter)
	 end
      end
   else
      current_display = nil
   end
end

screen_selection.mode = 'BROWSE'
function screen_selection:on_changed()
   local model, iter = self:get_selected()
   current_screen = model and model[iter][ScreenColumn.SCREEN]
end

window.child.display_open:get_child().halign = 'START'
window.child.display_close:get_child().halign = 'START'

local size_group = Gtk.SizeGroup()
size_group:add_widget(window.child.displays_box)
size_group:add_widget(window.child.screens_box)

function window.child.display_open:on_clicked()
   local dialog = Gtk.Dialog {
      title = "Open Display",
      transient_for = window,
      modal = true,
      buttons = {
	 { Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL },
	 { Gtk.STOCK_OK, Gtk.ResponseType.OK },
      },
   }
   dialog:set_default_response(Gtk.ResponseType.OK)
   local label = Gtk.Label {
      label = "Please enter the name of\nthe new display\n",
   }
   local entry = Gtk.Entry { activates_default = true }
   local content = dialog:get_content_area()
   content:add(label)
   content:add(entry)
   entry.has_focus = true
   dialog:show_all()
   while true do
      if dialog:run() ~= Gtk.ResponseType.OK then break end
      local name = entry.text
      if name ~= '' then
	 local display = Gdk.Display.open(name)
	 if display then break end
	 label.label = ("Can't open display :\n\t%s\n"
			.. "please try another one\n"):format(name)
      end
   end
   dialog:destroy()
end

local function change_display()
   local screen = window:get_screen()
   local display = screen:get_display()
   local popup = Gtk.Window {
      type = 'POPUP',
      window_position = 'CENTER',
      modal = true,
      transient_for = window,
      Gtk.Frame {
	 shadow_type = 'OUT',
	 Gtk.Label {
	    margin = 10,
	    label = "Please select the toplevel\n"
	       .. "to move to the new screen",
	 },
      },
   }
   popup:set_screen(screen)
   popup:show_all()

   local cursor = Gdk.Cursor.new_for_display(display, 'CROSSHAIR')
   local toplevel
   local device = Gtk.get_current_event_device()
   local grab_status = device:grab(
      popup.window, 'APPLICATION', false,
      'BUTTON_RELEASE_MASK', cursor, Gdk.CURRENT_TIME)
   if grab_status == 'SUCCESS' then
      -- Process events until user clicks.
      local clicked
      function popup:on_button_release_event()
	 clicked = true
	 return true
      end
      while not clicked do
	 GLib.MainContext.default():iteration(true)
      end

      -- Find toplevel at current pointer position.
      local gdk_window = device:get_window_at_position()
      local widget = gdk_window and gdk_window:get_widget()
      toplevel = widget and widget:get_toplevel()
      if toplevel == popup then toplevel = nil end
   end

   popup:destroy()
   -- Make sure that grab is really broken.
   Gdk.flush()

   -- Switch target window to selected screen.
   if toplevel then
      toplevel:set_screen(current_screen)
   else
      display:beep()
   end
end

function window:on_response(response_id)
   if response_id == Gtk.ResponseType.OK then
      change_display()
   else
      self:destroy()
   end
end

-- Adds new display into the list and hooks it do that it is
-- automatically removed when the display is closed.
local function add_display(display)
   local store = window.child.displays.model
   local iter = store:append {
      [DisplayColumn.NAME] = display:get_name(),
      [DisplayColumn.DISPLAY] = display,
   }
   local path = store:get_path(iter)
   local handler_id = display.on_closed:connect(
      function(is_error)
	 store:remove(store:get_iter(path))
      end)
   function window:on_destroy()
      GObject.signal_handler_disconnect(display, handler_id)
   end
end

-- Populate initial list of displays.
local display_manager = Gdk.DisplayManager.get()
for _, display in ipairs(display_manager:list_displays()) do
   add_display(display)
end

local handler_id = display_manager.on_display_opened:connect(
   function(display)
      add_display(display)
   end)
function window:on_destroy()
   GObject.signal_handler_disconnect(display_manager, handler_id)
end

window:show_all()
return window
end,

"Change Display",

table.concat {
   [[Demonstrates migrating a window between different displays ]],
   [[and screens. A display is a mouse and keyboard with some number ]],
   [[of associated monitors. A screen is a set of monitors grouped ]],
   [[into a single physical work area. The neat thing about having ]],
   [[multiple displays is that they can be on a completely separate ]],
   [[computers, as long as there is a network connection to ]],
   [[the computer where the application is running.
]],
   [[Only some of the windowing systems where GTK+ runs have the concept ]],
   [[of multiple displays and screens. (The X Window System is the main ]],
   [[example.) Other windowing systems can only handle one keyboard ]],
   [[and mouse, and combine all monitors into a single screen.
]],
   [[This is a moderately complex example, and demonstrates:
- Tracking the currently open displays and screens
- Changing the screen for a window
- Letting the user choose a window by clicking on it
- Using Gtk.ListStore and Gtk.TreeView
- Using Gtk.Dialog]]
}
