return function(parent, dir)

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local GdkPixbuf = lgi.GdkPixbuf

local assert = lgi.assert

-- Register icon.
Gtk.stock_add {
   Gtk.StockItem {
      stock_id = 'demo-gtk-logo',
      label = "_GTK!",
   }
}

local logo_pixbuf = GdkPixbuf.Pixbuf.new_from_file(
   dir:get_child('gtk-logo-rgb.gif'):get_path())
logo_pixbuf = logo_pixbuf and logo_pixbuf:add_alpha(true, 0xff, 0xff, 0xff)
local icon_factory = Gtk.IconFactory()
icon_factory:add_default()
icon_factory:add('demo-gtk-logo', Gtk.IconSet.new_from_pixbuf(logo_pixbuf))

local window = Gtk.Window {
   title = "Application Window",
   icon_name = 'document-open',
   default_width = 200,
   default_height = 200,
}

local function activate_action(action)
   local dialog = Gtk.MessageDialog {
      transient_for = window, destroy_with_parent = true,
      message_type = 'INFO', buttons = 'CLOSE',
      text = ("You activated action: '%s' of type '%s'"):format(
	 action.name, GObject.Type.name(action._type)),
      on_response = Gtk.Widget.destroy
   }
   dialog:show()
end

local message_label = Gtk.Label()
message_label:show()
local function change_radio_action(action)
   if action.active then
      message_label.label = (("You activated radio action: '%s' of type '%s'."
			      .."\nCurrent value: %d"):format(
			      action.name, GObject.Type.name(action._type),
			      action.value))
      window.child.infobar.message_type = action.value
      window.child.infobar:show()
   end
end

local function set_theme(action)
   local settings = Gtk.Settings.get_default()
   settings.gtk_application_prefer_dark_theme = action.active
end

local function about_cb()
   local about = Gtk.AboutDialog {
      program_name = "GTK+ Lgi Code Demos",
      version = ("Running against GTK+ %d.%d.%d, Lgi %s"):format(
	 Gtk.get_major_version(),
	 Gtk.get_minor_version(),
	 Gtk.get_micro_version(),
	 lgi._VERSION),
      copyright = "(C) 2012 Pavel Holejsovsky",
      license_type = 'MIT_X11',
      website = 'http://github.org/pavouk/lgi',
      comments = "Port of original GTK+ demo, (C) 1997-2009 The GTK+ Team",
      authors = {
	 "Pavel Holejsovsky",
	 "Peter Mattis",
	 "Spencer Kimball",
	 "Josh MacDonald",
	 "and many more...",
      },
      documenters = {
	 "Owen Taylor",
	 "Tony Gale",
	 "Matthias Clasen <mclasen@redhat.com>",
	 "and many more...",
      },
      logo = logo_pixbuf,
      title = "About GTK+ Code Demos",
   }
   about:run()
   about:hide()
end

local color = { RED = 1, GREEN = 2, BLUE = 3 }
local shape = { SQUARE = 1, RECTANGLE = 2, OVAL = 3 }

local action_group = Gtk.ActionGroup {
   name = 'AppWindowActions',
   Gtk.Action { name = 'FileMenu', label = "_File" },
   Gtk.Action { name = 'OpenMenu', label = "_Open" },
   Gtk.Action { name = 'PreferencesMenu', label = "_Preferences" },
   Gtk.Action { name = 'ColorMenu', label = "_Color" },
   Gtk.Action { name = 'ShapeMenu', label = "_Shape" },
   Gtk.Action { name = 'HelpMenu', label = "_Help" },
   { Gtk.Action { name = 'New', label = "_New", stock_id = Gtk.STOCK_NEW,
		  tooltip = "Create a new file",
		  on_activate = activate_action },
     accelerator = '<control>N' },
   Gtk.Action { name = 'File1', label = "File1", tooltip = "Open first file",
		on_activate = activate_action },
   Gtk.Action { name = 'Open', label = "_Open", stock_id = Gtk.STOCK_OPEN,
		tooltip = "Open a file",
		on_activate = activate_action },
   Gtk.Action { name = 'File1', label = "File1", tooltip = "Open first file",
		on_activate = activate_action },
   { Gtk.Action { name = 'Save', label = "_Save",
		  tooltip = "Save current file", stock_id = Gtk.STOCK_SAVE,
		  on_activate = activate_action },
     accelerator = '<control>S' },
   Gtk.Action { name = 'SaveAs', label = "Save _As",
		tooltip = "Save to a file", stock_id = Gtk.STOCK_SAVE,
		on_activate = activate_action },
   { Gtk.Action { name = 'Quit', label = "_Quit",
		  tooltip = "Quit", stock_id = Gtk.STOCK_QUIT,
		  on_activate = activate_action },
     accelerator = '<control>Q' },
   Gtk.Action { name = 'About', label = "_About",
		tooltip = "About",
		on_activate = about_cb },
   Gtk.Action { name = 'Logo', stock_id = 'demo-gtk-logo',
		tooltip = "GTK+ on LGI",
		on_activate = activate_action },

   { Gtk.ToggleAction { name = 'Bold', stock_id = Gtk.STOCK_BOLD,
			label = "_Bold", tooltip = "Bold",
			on_activate = activate_action,
			active = true },
     accelerator = '<control>B' },
   Gtk.ToggleAction { name = 'DarkTheme', label = "_Prefer dark theme",
		      tooltip = "Prefer dark theme", active = false,
		      on_activate = set_theme },
   {
      { Gtk.RadioAction { name = 'Red', label = "_Red", tooltip = "Blood",
			  value = color.RED, },
	accelerator = '<control>R' },
      { Gtk.RadioAction { name = 'Green', label = "_Green", tooltip = "Grass",
			  value = color.GREEN, },
	accelerator = '<control>G' },
      { Gtk.RadioAction { name = 'Blue', label = "_Blue", tooltip = "Sky",
			  value = color.BLUE, },
	accelerator = '<control>B' },
      on_change = change_radio_action,
   },

   {
      { Gtk.RadioAction { name = 'Square', label = "_Square",
			  tooltip = "Square",
			  value = shape.SQUARE, },
	accelerator = '<control>S' },
      { Gtk.RadioAction { name = 'Rectangle', label = "_Rectangle",
			  tooltip = "Rectangle", value = shape.RECTANGLE, },
	accelerator = '<control>R' },
      { Gtk.RadioAction { name = 'Oval', label = "_Oval",
			  tooltip = "Oval", value = shape.OVAL, },
	accelerator = '<control>O' },
      on_change = change_radio_action,
   },
}

local merge = Gtk.UIManager {}
merge:insert_action_group (action_group, 0)
window:add_accel_group(merge:get_accel_group())
assert(merge:add_ui_from_string([[
<ui>
  <menubar name='MenuBar'>
    <menu action='FileMenu'>
      <menuitem action='New'/>
      <menuitem action='Open'/>
      <menuitem action='Save'/>
      <menuitem action='SaveAs'/>
      <separator/>
      <menuitem action='Quit'/>
    </menu>
    <menu action='PreferencesMenu'>
      <menuitem action='DarkTheme'/>
      <menu action='ColorMenu'>
       <menuitem action='Red'/>
       <menuitem action='Green'/>
       <menuitem action='Blue'/>
      </menu>
      <menu action='ShapeMenu'>
        <menuitem action='Square'/>
        <menuitem action='Rectangle'/>
        <menuitem action='Oval'/>
      </menu>
      <menuitem action='Bold'/>
    </menu>
    <menu action='HelpMenu'>
      <menuitem action='About'/>
    </menu>
  </menubar>
  <toolbar name='ToolBar'>
    <toolitem action='Open'>
      <menu action='OpenMenu'>
        <menuitem action='File1'/>
      </menu>
    </toolitem>
    <toolitem action='Quit'/>
    <separator action='Sep1'/>
    <toolitem action='Logo'/>
  </toolbar>
</ui>]], -1))

local menubar = merge:get_widget('/MenuBar')
menubar.halign = 'FILL'
local toolbar = merge:get_widget('/ToolBar')
toolbar.halign = 'FILL'

window.child = Gtk.Grid {
   orientation = 'VERTICAL',
   menubar,
   toolbar,
   Gtk.InfoBar {
      id = 'infobar',
      no_show_all = true,
      halign = 'FILL',
      on_response = Gtk.Widget.hide
   },
   Gtk.ScrolledWindow {
      shadow_type = 'IN',
      halign = 'FILL', valign = 'FILL',
      expand = true,
      Gtk.TextView {
	 id = 'view',
      },
   },
   Gtk.Statusbar {
      id = 'statusbar',
      halign = 'FILL',
   },
}

local buffer = window.child.view.buffer
local statusbar = window.child.statusbar

-- Updates statusbar according to the buffer of the view
local function update_statusbar()
   statusbar:pop(0)
   local iter = buffer:get_iter_at_mark(buffer:get_insert())
   local msg =
   statusbar:push(
      0, ("Cursor at row %d column %d - %d chars in document"):format(
	 iter:get_line(), iter:get_line_offset(), buffer:get_char_count()))
end

function buffer:on_changed()
   update_statusbar()
end

function buffer:on_mark_set()
   update_statusbar()
end

-- Perform initial statusbar update.
update_statusbar()

-- Add infobar area.
local info_area = window.child.infobar:get_content_area()
info_area:add(message_label)
window.child.infobar:add_button(Gtk.STOCK_OK, Gtk.ResponseType.OK)

window:show_all()
window.child.view:grab_focus()
return window

end,

"Application main window",

[[Demonstrates a typical application window with menubar, toolbar, statusbar.]]
