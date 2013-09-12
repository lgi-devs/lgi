return function(parent, dir)

local coroutine = require 'coroutine'
local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local window = Gtk.Dialog {
   title = "Search Entry",
   resizable = false,
   on_response = Gtk.Widget.destroy,
   buttons = {
      { Gtk.STOCK_CLOSE, Gtk.ResponseType.NONE },
   },
}

local content = Gtk.Box {
   orientation = 'VERTICAL',
   spacing = 5,
   border_width = 5,
   Gtk.Label {
      label = "Search entry demo",
      use_markup = true,
   },
   Gtk.Box {
      orientation = 'HORIZONTAL',
      spacing = 10,
      Gtk.Entry {
	 id = 'entry',
	 secondary_icon_stock = Gtk.STOCK_CLEAR,
      },
      Gtk.Notebook {
	 id = 'buttons',
	 show_tabs = false,
	 show_border = false,
	 Gtk.Button {
	    id = 'button_find',
	    label = "Find",
	    sensitive = false,
	 },
	 Gtk.Button {
	    id = 'button_cancel',
	    label = "Cancel",
	 },
      },
   },
}
window:get_content_area():add(content)
local entry = content.child.entry

local search = {
   {
      menu = "Search by _name",
      stock = Gtk.STOCK_FIND,
      placeholder = "name",
      tooltip = "Search by name\n"
	 .. "Click here to change the search type",
   },
   {
      menu = "Search by _description",
      stock = Gtk.STOCK_EDIT,
      placeholder = "description",
      tooltip = "Search by description\n"
	 .. "Click here to change the search type",
   },
   {
      menu = "Search by _file name",
      stock = Gtk.STOCK_OPEN,
      placeholder = "filename",
      tooltip = "Search by file name\n"
	 .. "Click here to change the search type",
   },
}

local function search_by(method)
   entry.primary_icon_stock = method.stock
   entry.primary_icon_tooltip_text = method.tooltip
   entry.placeholder_text = method.placeholder
end
search_by(search[1])

local function create_search_menu()
   local menu = Gtk.Menu { visible = true }
   for i = 1, #search do
      menu:append(
	 Gtk.ImageMenuItem {
	    image = Gtk.Image {
	       stock = search[i].stock,
	       icon_size = Gtk.IconSize.MENU
	    },
	    label = search[i].menu,
	    use_underline = true,
	    visible = true,
	    always_show_image = true,
	    on_activate = function()
	       search_by(search[i])
	    end,
	 })
   end
   return menu
end
local menu = create_search_menu()
menu:attach_to_widget(entry)

function entry:on_populate_popup(menu)
   for _, item in ipairs {
      Gtk.SeparatorMenuItem {},
      Gtk.MenuItem {
	 label = "C_lear",
	 use_underline = true,
	 visible = true,
	 on_activate = function()
	    entry.text = ''
	 end
      },
      Gtk.MenuItem {
	 label = "Search by",
	 submenu = create_search_menu(),
	 visible = true,
      },
   } do
      item.visible = true
      menu:append(item)
   end
end

function entry:on_icon_press(position, event)
   if position == 'PRIMARY' then
      menu:popup(nil, nil, nil, nil, event.button, event.time)
   else
      entry.text = ''
   end
end

function entry.on_notify:text(pspec)
   local has_text = entry.text ~= ''
   entry.secondary_icon_sensitive = has_text
   content.child.button_find.sensitive = has_text
end

local search_progress_id, finish_search_id

local function finish_search()
   content.child.buttons.page = 0
   if search_progress_id then
      GLib.source_remove(search_progress_id)
      search_progress_id = nil
   end
   if finish_search_id then
      GLib.source_remove(finish_search_id)
      finish_search_id = nil
   end
   entry.progress_fraction = 0
end

local function start_search()
   content.child.buttons.page = 1
   search_progress_id =
      GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1,
			       function()
				  search_progress_id = GLib.timeout_add(
				     GLib.PRIORITY_DEFAULT, 100,
				     function()
					entry:progress_pulse()
					return true
				     end)
				  return false
			       end)
   finish_search_id = GLib.timeout_add_seconds(
      GLib.PRIORITY_DEFAULT, 15, finish_search)
end

function entry:on_activate()
   if not search_progress_id then
      start_search()
   end
end

function content.child.button_find:on_clicked()
   start_search()
end

function content.child.button_cancel:on_clicked()
   finish_search()
end

function window:on_destroy()
   finish_search()
end

window:show_all()
return window
end,

"Entry/Search Entry",

table.concat {
   "Gtk.Entry allows to display icons and progress information. ",
   "This demo shows how to use these features in a search entry."
}
