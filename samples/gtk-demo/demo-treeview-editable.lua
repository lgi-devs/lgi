return function(parent, dir)

local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local ItemColumn = {
   NUMBER = 1,
   PRODUCT = 2,
   YUMMY = 3,
}

local NumberColumn = {
   TEXT = 1,
   NUMBER = 2,
}

-- Fill store with initial items.
local item_store = Gtk.ListStore.new {
   [ItemColumn.NUMBER] = GObject.Type.INT,
   [ItemColumn.PRODUCT] = GObject.Type.STRING,
   [ItemColumn.YUMMY] = GObject.Type.INT,
}

for _, item in ipairs {
   {
      [ItemColumn.NUMBER] = 3,
      [ItemColumn.PRODUCT] = "bottles of coke",
      [ItemColumn.YUMMY] = 20,
   },
   {
      [ItemColumn.NUMBER] = 5,
      [ItemColumn.PRODUCT] = "packages of noodles",
      [ItemColumn.YUMMY] = 50,
   },
   {
      [ItemColumn.NUMBER] = 2,
      [ItemColumn.PRODUCT] = "packages of chocolate chip cookies",
      [ItemColumn.YUMMY] = 90,
   },
   {
      [ItemColumn.NUMBER] = 1,
      [ItemColumn.PRODUCT] = "can vanilla ice cream",
      [ItemColumn.YUMMY] = 60,
   },
   {
      [ItemColumn.NUMBER] = 6,
      [ItemColumn.PRODUCT] = "eggs",
      [ItemColumn.YUMMY] = 10,
   },
} do
   item_store:append(item)
end

-- Fill store with numbers.
local number_store = Gtk.ListStore.new {
   [NumberColumn.TEXT] = GObject.Type.INT,
   [NumberColumn.NUMBER] = GObject.Type.STRING,
}

for i = 1, 10 do
   number_store:append {
      [NumberColumn.TEXT] = i,
      [NumberColumn.NUMBER] = i,
   }
end

local window = Gtk.Window {
   title = "Shopping list",
   default_width = 320,
   default_height = 200,
   border_width = 5,
   Gtk.Box {
      orientation = 'VERTICAL',
      spacing = 5,
      Gtk.Label {
	 label = "Shopping list (you can edit the cells!)",
      },
      Gtk.ScrolledWindow {
	 shadow_type = 'ETCHED_IN',
	 expand = true,
	 Gtk.TreeView {
	    id = 'view',
	    model = item_store,
	    Gtk.TreeViewColumn {
	       title = "Number",
	       {
		  Gtk.CellRendererCombo {
		     id = 'number_renderer',
		     model = number_store,
		     text_column = NumberColumn.TEXT,
		     has_entry = false,
		     editable = true
		  },
		  { text = ItemColumn.NUMBER, }
	       },
	    },
	    Gtk.TreeViewColumn {
	       title = "Product",
	       {
		  Gtk.CellRendererText {
		     id = 'product_renderer',
		     editable = true,
		  },
		  { text = ItemColumn.PRODUCT, }
	       }
	    },
	    Gtk.TreeViewColumn {
	       title = "Yummy",
	       {
		  Gtk.CellRendererProgress {},
		  {
		     value = ItemColumn.YUMMY,
		  },
	       }
	    },
	 },
      },
      Gtk.Box {
	 orientation = 'HORIZONTAL',
	 spacing = 4,
	 homogeneous = true,
	 Gtk.Button {
	    id = 'add',
	    label = "Add item",
	 },
	 Gtk.Button {
	    id = 'remove',
	    label = "Remove item",
	 },
      },
   }
}

function window.child.number_renderer:on_edited(path_string, new_text)
   local path = Gtk.TreePath.new_from_string(path_string)
   item_store[path][ItemColumn.NUMBER] = new_text
end

function window.child.number_renderer:on_editing_started(editable, path)
   editable:set_row_separator_func(
      function(model, iter)
	 return model:get_path(iter):get_indices()[1] == 5
      end)
end

function window.child.product_renderer:on_edited(path_string, new_text)
   local path = Gtk.TreePath.new_from_string(path_string)
   item_store[path][ItemColumn.PRODUCT] = new_text
end

local selection = window.child.view:get_selection()
selection.mode = 'SINGLE'

function window.child.add:on_clicked()
   item_store:append {
      [ItemColumn.NUMBER] = 0,
      [ItemColumn.PRODUCT] = "Description here",
      [ItemColumn.YUMMY] = 50,
   }
end

function window.child.remove:on_clicked()
   local model, iter = selection:get_selected()
   if model and iter then
      model:remove(iter)
   end
end

window:show_all()
return window
end,

"Tree View/Editable Cells",

table.concat {
   [[This demo demonstrates the use of editable cells in a Gtk.TreeView. ]],
   [[If you're new to the Gtk.TreeView widgets and associates, look into ]],
   [[the Gtk.ListStore example first. It also shows how to use ]],
   [[the Gtk.CellRenderer::editing-started signal to do custom setup of ]],
   [[the editable widget.
The cell renderers used in this demo are Gtk.CellRendererText, ]],
   [[Gtk.CellRendererCombo and GtkCell.RendererProgress.]]
}
