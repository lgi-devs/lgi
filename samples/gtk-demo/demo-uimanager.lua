return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local log = lgi.log.domain('uimanager-demo')

local function activate_action(action)
   log.message('Action "%s" activated', action.name)
end

local function activate_radio_action(action)
   log.message('Radio action "%s" selected', action.name)
end

local COLOR = { RED = 1, GREEN = 2, BLUE = 3 }
local SHAPE = { SQUARE = 1, RECTANGLE = 2, OVAL = 3 }

local actions = Gtk.ActionGroup {
   name = 'Actions',
   Gtk.Action { name = 'FileMenu', label = "_File" },
   Gtk.Action { name = 'PreferencesMenu', label = "_Preferences" },
   Gtk.Action { name = 'ColorMenu', label = "_Color" },
   Gtk.Action { name = 'ShapeMenu', label = "_Shape" },
   Gtk.Action { name = 'HelpMenu', label = "_Help" },
   { Gtk.Action { name = 'New', stock_id  = Gtk.STOCK_NEW, label = "_New",
		  tooltip = "Create a new file",
		  on_activate = activate_action, },
     accelerator = '<control>N', },
   { Gtk.Action { name = 'Open', stock_id = Gtk.STOCK_OPEN, label = "_Open",
		  tooltip = "Open a file",
		  on_activate = activate_action, },
     accelerator = '<control>O', },
   { Gtk.Action { name = 'Save', stock_id = Gtk.STOCK_SAVE, label = "_Save",
		  tooltip = "Save current file",
		  on_activate = activate_action, },
     accelerator = '<control>S', },
   Gtk.Action { name = 'SaveAs', stock_id = Gtk.STOCK_SAVE,
		label = "Save _As...", tooltip = "Save to a file",
		on_activate = activate_action, },
   { Gtk.Action { name = 'Quit', stock_id = Gtk.STOCK_QUIT, label = "_Quit",
		tooltip = "Quit",
		on_activate = activate_action, },
     accelerator = '<control>Q', },
   { Gtk.Action { name = 'About', stock_id = Gtk.STOCK_ABOUT, label = "_About",
		  tooltip = "About",
		  on_activate = activate_action, },
     accelerator = '<control>A', },
   Gtk.Action { name = 'Logo', stock_id = 'demo-gtk-logo', tooltip = "GTK+",
		on_activate = activate_action, },
   { Gtk.ToggleAction { name = 'Bold', stock_id = Gtk.STOCK_BOLD,
			label = "_Bold", tooltip = "Bold", active = true,
			on_activate = activate_action },
     accelerator = "<control>B", },
   {
      { Gtk.RadioAction { name = 'Red', label = "_Red", tooltip = "Blood",
			  value = COLOR.RED, active = true, },
	accelerator = '<control>R', },
      { Gtk.RadioAction { name = 'Green', label = "_Green", tooltip = "Grass",
			  value = COLOR.GREEN },
	accelerator = '<control>G', },
      { Gtk.RadioAction { name = 'Blue', label = "_Blue", tooltip = "Sky",
			  value = COLOR.BLUE },
	accelerator = '<control>B', },
      on_change = activate_radio_action,
   },
   {
      { Gtk.RadioAction { name = 'Square', label = "_Square",
			  tooltip = "Square", value = SHAPE.SQUARE, },
	accelerator = '<control>S', },
      { Gtk.RadioAction { name = 'Rectangle', label = "_Rectangle",
			  tooltip = "Rectangle", value = SHAPE.RECTANGLE },
	accelerator = '<control>R', },
      { Gtk.RadioAction { name = 'Oval', label = "_Oval",
			  tooltip = "Oval", value = SHAPE.OVAL, active = true },
	accelerator = '<control>O', },
      on_change = activate_radio_action,
   },
}

local ui = Gtk.UIManager()
ui:insert_action_group(actions, 0)

local ok, err = ui:add_ui_from_string(
   [[
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
  <toolbar  name='ToolBar'>
    <toolitem action='Open'/>
    <toolitem action='Quit'/>
    <separator action='Sep1'/>
    <toolitem action='Logo'/>
  </toolbar>
</ui>]], -1)

if not ok then
   log.message('building menus failed: %s', err)
end

local window = Gtk.Window {
   title = "UI Manager",
   Gtk.Box {
      orientation = 'VERTICAL',
      ui:get_widget('/MenuBar'),
      Gtk.Label {
	 id = 'label',
	 label = "Type\n<alt>\n to start",
	 halign = 'CENTER',
	 valign = 'CENTER',
	 expand = true,
      },
      Gtk.Separator {
	 orientation = 'HORIZONTAL',
      },
      Gtk.Box {
	 orientation = 'VERTICAL',
	 spacing = 10,
	 border_width = 10,
	 Gtk.Button {
	    id = 'close',
	    label = "close",
	    can_default = true,
	 },
      },
   },
}

window:add_accel_group(ui:get_accel_group())

function window.child.close:on_clicked()
   window:destroy()
end

window.child.close:grab_default()

window.child.label:set_size_request(200, 200)
window:show_all()
return window
end,

"UI Manager",

table.concat {
   [[The Gtk.UIManager object allows the easy creation of menus from ]],
   [[an array of actions and a description of the menu hierarchy.]],
}
