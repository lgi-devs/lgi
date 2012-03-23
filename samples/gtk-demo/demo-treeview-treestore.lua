return function(parent, dir)

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local Column = {
   HOLIDAY_NAME = 1,
   ALEX = 2,
   HAVOC = 3,
   TIM = 4,
   OWEN = 5,
   DAVE = 6,
   VISIBLE = 7,
   WORLD = 8,
}

local store = Gtk.TreeStore.new {
   [Column.HOLIDAY_NAME] = GObject.Type.STRING,
   [Column.ALEX] = GObject.Type.BOOLEAN,
   [Column.HAVOC] = GObject.Type.BOOLEAN,
   [Column.TIM] = GObject.Type.BOOLEAN,
   [Column.OWEN] = GObject.Type.BOOLEAN,
   [Column.DAVE] = GObject.Type.BOOLEAN,
   [Column.VISIBLE] = GObject.Type.BOOLEAN,
   [Column.WORLD] = GObject.Type.BOOLEAN,
}

for _, month in ipairs {
   { "January", {
	{ "New Years Day", true, true, true, true, false, true, },
	{ "Presidential Inauguration", false, true, false, true, false, false },
	{"Martin Luther King Jr. day", false, true, false, true, false, false },
     }
  },
   { "February", {
	{ "Presidents' Day", false, true, false, true, false, false },
	{ "Groundhog Day", false, false, false, false, false, false },
	{ "Valentine's Day", false, false, false, false, true, true },
     }
  },
   { "March", {
	{ "National Tree Planting Day", false, false, false, false, false, false },
	{ "St Patrick's Day", false, false, false, false, false, true },
     }
  },
   { "April", {
	{ "April Fools' Day", false, false, false, false, false, true },
	{ "Army Day", false, false, false, false, false, false },
	{ "Earth Day", false, false, false, false, false, true },
	{ "Administrative Professionals' Day", false, false, false, false, false, false },
     }
  },
   { "May", {
	{ "Nurses' Day", false, false, false, false, false, false },
	{ "National Day of Prayer", false, false, false, false, false, false },
	{ "Mothers' Day", false, false, false, false, false, true },
	{ "Armed Forces Day", false, false, false, false, false, false },
	{ "Memorial Day", true, true, true, true, false, true },
     }
  },
   { "June", {
	{ "June Fathers' Day", false, false, false, false, false, true },
	{ "Juneteenth (Liberation of Slaves)", false, false, false, false, false, false },
	{ "Flag Day", false, true, false, true, false, false },
     }
  },
   { "July", {
	{ "Parents' Day", false, false, false, false, false, true },
	{ "Independence Day", false, true, false, true, false, false },
     }
  },
   { "August", {
	{ "Air Force Day", false, false, false, false, false, false },
	{ "Coast Guard Day", false, false, false, false, false, false },
	{ "Friendship Day", false, false, false, false, false, false },
     }
  },
   { "September", {
	{ "Grandparents' Day", false, false, false, false, false, true },
	{ "Citizenship Day or Constitution Day", false, false, false, false, false, false },
	{ "Labor Day", true, true, true, true, false, true },
     }
  },
   { "October", {
	{ "National Children's Day", false, false, false, false, false, false },
	{ "Bosses' Day", false, false, false, false, false, false },
	{ "Sweetest Day", false, false, false, false, false, false },
	{ "Mother-in-Law's Day", false, false, false, false, false, false },
	{ "Navy Day", false, false, false, false, false, false },
	{ "Columbus Day", false, true, false, true, false, false },
	{ "Halloween", false, false, false, false, false, true },
     }
  },
   { "November", {
	{ "Marine Corps Day", false, false, false, false, false, false },
	{ "Veterans' Day", true, true, true, true, false, true },
	{ "Thanksgiving", false, true, false, true, false, false },
     }
  },
   { "December", {
	{ "Pearl Harbor Remembrance Day", false, false, false, false, false, false },
	{ "Christmas", true, true, true, true, false, true },
	{ "Kwanzaa", false, false, false, false, false, false },
     }
  },
} do
   local iter = store:append(nil, { [Column.HOLIDAY_NAME] = month[1] })
   for _, holiday in ipairs(month[2]) do
      store:append(iter, {
		      [Column.HOLIDAY_NAME] = holiday[1],
		      [Column.ALEX] = holiday[2],
		      [Column.HAVOC] = holiday[3],
		      [Column.TIM] = holiday[4],
		      [Column.OWEN] = holiday[5],
		      [Column.DAVE] = holiday[6],
		      [Column.VISIBLE] = true,
		      [Column.WORLD] = holiday[7],
		   })
   end
end

local window = Gtk.Window {
   title = "Card planning sheet",
   default_width = 650,
   default_height = 400,
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 8,
      border_width = 8,
      Gtk.Label {
	 label = "Jonathan's Holiday Card Planning Sheet",
      },
      Gtk.ScrolledWindow {
	 shadow_type = 'ETCHED_IN',
	 expand = true,
	 Gtk.TreeView {
	    id = 'view',
	    model = store,
	    rules_hint = true,
	    Gtk.TreeViewColumn {
	       title = "Holiday",
	       { Gtk.CellRendererText {}, { text = Column.HOLIDAY_NAME } },
	    },
	 },
      },
   }
}

local view = window.child.view
local selection = view:get_selection()
selection.mode = 'MULTIPLE'

-- Add columns programmatically.
for _, info in ipairs {
   { Column.ALEX, "Alex", true },
   { Column.HAVOC, "Havoc" },
   { Column.TIM, "Tim", true },
   { Column.OWEN, "Owen" },
   { Column.DAVE, "Dave" },
} do
   -- Prepare renderer and connect its on_toggled signal.
   local col = info[1]
   local renderer = Gtk.CellRendererToggle {
      xalign = 0,
   }
   function renderer:on_toggled(path_str)
      local row = store[Gtk.TreePath.new_from_string(path_str)]
      row[col] = not row[col]
   end


   -- Add new column to the view.
   view:append_column(
      Gtk.TreeViewColumn {
	 title = info[2],
	 sizing = 'FIXED',
	 fixed_width = 50,
	 clickable = true,
	 {
	    renderer,
	    {
	       active = col,
	       visible = Column.VISIBLE,
	       activatable = info[3] and Column.WORLD or nil,
	    }
	 }
      })
end

-- Expand all rows after treeview has been realized.
view.on_realize = view.expand_all

window:show_all()
return window
end,

"Tree View/Tree Store",

table.concat {
   [[The Gtk.TreeStore is used to store data in tree form, to be used later ]],
   [[on by a Gtk.TreeView to display it. This demo builds a simple ]],
   [[Gtk.TreeStore and displays it. If you're new to the Gtk.TreeView ]],
   [[widgets and associates, look into the Gtk.ListStore example first.]],
}
