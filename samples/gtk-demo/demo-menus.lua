return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local function create_menu(depth, tearoff)
   if depth < 1 then return nil end
   local menu = Gtk.Menu()
   if tearoff then
      menu:append(Gtk.TearoffMenuItem { visible = true })
   end
   local group = nil
   for i = 1, 5 do
      local item = Gtk.RadioMenuItem {
	 group = group,
	 label = ("item %2d - %d"):format(depth, i),
	 submenu = create_menu(depth - 1, true),
	 sensitive = (i ~= 4),
      }
      if not group then group = item end
      menu:append(item)
   end
   return menu
end

local window = Gtk.Window {
   title = "Menus",
   Gtk.Box {
      orientation = 'VERTICAL',
      Gtk.MenuBar {
	 id = 'menubar',
	 Gtk.MenuItem {
	    label = "test\nline2",
	    visible = true,
	    submenu = create_menu(2, true),
	 },
	 Gtk.MenuItem {
	    label = "foo",
	    visible = true,
	    submenu = create_menu(3),
	 },
	 Gtk.MenuItem {
	    label = "bar",
	    visible = true,
	    submenu = create_menu(4, true),
	 },
      },
      Gtk.Box {
	 orientation = 'VERTICAL',
	 spacing = 10,
	 border_width = 10,
	 Gtk.Button {
	    id = 'flip',
	    label = "Flip",
	 },
	 Gtk.Button {
	    id = 'close',
	    label = "Close",
	 },
      },
   },
}

function window.child.close:on_clicked()
   window:destroy()
end

function window.child.flip:on_clicked()
   local menubar = window.child.menubar
   local orientation = menubar.parent.orientation
   orientation = (orientation == 'HORIZONTAL'
		  and 'VERTICAL' or 'HORIZONTAL')
   menubar.parent.orientation = orientation
   menubar.pack_direction = (orientation == 'VERTICAL'
			     and 'LTR' or 'TTB')
end

window:show_all()
return window
end,

"Menus",

table.concat {
   [[There are several widgets involved in displaying menus. ]],
   [[The Gtk.MenuBar widget is a menu bar, which normally appears ]],
   [[horizontally at the top of an application, but can also be ]],
   [[layed out vertically. The Gtk.Menu widget is the actual menu ]],
   [[that pops up. Both Gtk.MenuBar and Gtk.Menu are subclasses ]],
   [[of Gtk.MenuShell; a Gtk.MenuShell contains menu items (Gtk.MenuItem). ]],
   [[Each menu item contains text and/or images and can be selected ]],
   [[by the user.
]],
   [[There are several kinds of menu item, including plain ]],
   [[Gtk.MenuItem, Gtk.CheckMenuItem which can be checked/unchecked, ]],
   [[Gtk.RadioMenuItem which is a check menu item that's in a mutually ]],
   [[exclusive group, Gtk.SeparatorMenuItem which is a separator bar, ]],
   [[Gtk.TearoffMenuItem which allows a Gtk.Menu to be torn off, ]],
   [[and Gtk.ImageMenuItem which can place a Gtk.Image or other widget ]],
   [[next to the menu text.
]],
   [[A Gtk.MenuItem can have a submenu, which is simply a Gtk.Menu ]],
   [[to pop up when the menu item is selected. Typically, all menu ]],
   [[items in a menu bar have submenus.
]],
   [[Gtk.UIManager provides a higher-level interface for creating ]],
   [[menu bars and menus; while you can construct menus manually, ]],
   [[most people don't do that. There's a separate demo for ]],
   [[Gtk.UIManager.]],
}
