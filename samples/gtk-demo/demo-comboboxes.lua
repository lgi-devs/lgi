return function(parent, dir)

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk
local GdkPixbuf = lgi.GdkPixbuf

local ComboColumn = {
   PIXBUF = 1,
   TEXT = 2,
}

local function create_stock_icon_store()
   local cellview = Gtk.CellView {}
   local store = Gtk.ListStore.new {
      [ComboColumn.PIXBUF] = GdkPixbuf.Pixbuf,
      [ComboColumn.TEXT] = GObject.Type.STRING
   }
   for _, stock in ipairs {
      Gtk.STOCK_DIALOG_WARNING,
      Gtk.STOCK_STOP,
      Gtk.STOCK_NEW,
      Gtk.STOCK_CLEAR,
      '',
      Gtk.STOCK_OPEN,
   } do
      if stock ~= '' then
	 store:append {
	    [ComboColumn.PIXBUF] = cellview:render_icon_pixbuf(
	       stock, Gtk.IconSize.BUTTON),
	    [ComboColumn.TEXT] = Gtk.stock_lookup(stock).label:gsub('_', '')
	 }
      else
	 store:append {
	    [ComboColumn.TEXT] = 'separator'
	 }
      end
   end
   return store
end

local function set_sensitive(layout, cell, model, iter)
   local indices = model:get_path(iter):get_indices()
   cell.sensitive = (indices[1] ~= 1)
end

local function create_capital_store()
   local store = Gtk.TreeStore.new { GObject.Type.STRING }
   for _, group in ipairs {
      { name = "A - B",
	"Albany", "Annapolis", "Atlanta", "Augusta", "Austin",
	"Baton Rouge", "Bismarck", "Boise", "Boston" },
      { name = "C - D",
	"Carson City", "Charleston", "Cheyenne", "Columbia", "Columbus",
	"Concord", "Denver", "Des Moines", "Dover" },
      { name = "E - J",
	"Frankfort", "Harrisburg", "Hartford", "Helena", "Honolulu",
	"Indianapolis", "Jackson", "Jefferson City", "Juneau" },
      { name = "K - O",
	"Lansing", "Lincoln", "Little Rock", "Madison", "Montgomery",
	"Montpelier", "Nashville", "Oklahoma City", "Olympia" },
      { name = "P - S",
	"Phoenix", "Pierre", "Providence", "Raleigh", "Richmond",
	"Sacramento", "Salem", "Salt Lake City", "Santa Fe",
	"Springfield", "St. Paul" },
      { name = "T - Z",
	"Tallahassee", "Topeka", "Trenton" },
   } do
      local gi = store:append(nil, { [1] = group.name })
      for _, city in ipairs(group) do
	 store:append(gi, { [1] = city })
      end
   end
   return store
end

local function is_capital_sensitive(layout, cell, model, iter)
   cell.sensitive = not model:iter_has_child(iter)
end

local window = Gtk.Window {
   title = "Combo boxes",
   border_width = 10,
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 10,
      Gtk.Frame {
	 label = "Some stock icons",
	 Gtk.Box {
	    orientation = 'VERTICAL',
	    border_width = 5,
	    Gtk.ComboBox {
	       id = 'icons',
	       model = create_stock_icon_store(),
	       active = 0,
	       cells = {
		  {
		     Gtk.CellRendererPixbuf(),
		     { pixbuf = ComboColumn.PIXBUF },
		     align = 'start',
		     data_func = set_sensitive,
		  },
		  {
		     Gtk.CellRendererText(),
		     { text = ComboColumn.TEXT },
		     align = 'start',
		     data_func = set_sensitive,
		  },
	       },
	    },
	 },
      },
      Gtk.Frame {
	 label = "Where are we?",
	 Gtk.Box {
	    orientation = 'VERTICAL',
	    border_width = 5,
	    Gtk.ComboBox {
	       id = 'capitals',
	       model = create_capital_store(),
	       cells = {
		  {
		     Gtk.CellRendererText(),
		     { text = 1 },
		     align = 'start',
		     data_func = is_capital_sensitive,
		  }
	       }
	    },
	 },
      },
      Gtk.Frame {
	 label = "Editable",
	 Gtk.Box {
	    orientation = 'VERTICAL',
	    border_width = 5,
	    Gtk.ComboBoxText {
	       id = 'entry',
	       has_entry = true,
	       entry_text_column = 0,
	    },
	 },
      },
      Gtk.Frame {
	 label = "String IDs",
	 Gtk.Box {
	    orientation = 'VERTICAL',
	    border_width = 5,
	    Gtk.ComboBoxText {
	       id = 'stringids',
	       entry_text_column = 0,
	       id_column = 1,
	    },
	    Gtk.Entry { id = 'ids_entry' },
	 },
      },
   },
}

window.child.icons:set_row_separator_func(
   function(model, iter)
      return model:get_path(iter):get_indices()[1] == 4
   end)

local capitals = window.child.capitals
capitals:set_active_iter(capitals.model:get_iter(
			    Gtk.TreePath.new_from_string('0:8')))

for _, label in ipairs { "One", "Two", "2½", "Three" } do
   window.child.entry:append_text(label)
end
local entry = window.child.entry:get_child()
local allowed = { ["One"] = true, ["Two"] = true,
		  ["2½"] = true, ["Three"] = true }
function entry:on_changed()
   local color
   if not self.text:match('^[0-9]*$') and not allowed[self.text] then
      color = Gdk.RGBA { red = 1, green = 0.9, blue = 0.9, alpha = 1 }
   end
   self:override_color(0, color)
end

for id, text in pairs {
   never = "Not visible",
   when_active = "Visible when active",
   always = "Always visible",
   } do
   window.child.stringids:append(id, text)
end

window.child.stringids:bind_property(
   'active-id', window.child.ids_entry, 'text', 'BIDIRECTIONAL')

window:show_all()
return window
end,

"Combo boxes",

table.concat {
   "The ComboBox widget allows to select one option out of a list. ",
   "The ComboBoxEntry additionally allows the user to enter a value ",
   "that is not in the list of options.\n",
   "How the options are displayed is controlled by cell renderers.",
}
