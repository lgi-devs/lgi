return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local assert = lgi.assert

local builder = Gtk.Builder()
assert(builder:add_from_file(dir:get_child('demo.ui'):get_path()))
local ui = builder.objects

-- Get top-level window from the builder.
local window = ui.window1

-- Connect 'Quit' and 'About' actions.
function ui.Quit:on_activate()
   window:destroy()
 end

function ui.About:on_activate()
   ui.aboutdialog1:run()
   ui.aboutdialog1:hide()
end

window:show_all()
return window
end,

"Builder",

table.concat {
   [[Demonstrates an interface loaded from a XML description.]],
}
