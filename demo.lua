-- Demo modified from https://github.com/lgi-devs/lgi/issues/333 - the stated issues should be resolved.

-- hack to require lgi from this directory before /usr/ or wherever windows installs it.
local sep = package.path:match("[/\\]")
package.path = ("./?.lua;./?/init.lua;"):gsub("/", sep) .. package.path

local lgi, lgi_path = require("lgi")

-- check the hack worked
assert(lgi_path:match("%.[/\\]lgi.lua"), "Loaded wrong LGI!")

local Gtk = lgi.require("Gtk", "3.0")

-- This button demonstrates that re-assigning a signal handler doesn't disconnect the previous assignment:
local button_1 = Gtk.Button.new_with_label("I can be clicked multiple times, but I have multiple handlers")

---@diagnostic disable-next-line:duplicate-set-field
button_1.on_clicked = function()
    print("Button 2 Click handler 1!")
end
---@diagnostic disable-next-line:duplicate-set-field
button_1.on_clicked = function()
    print("Button 2 Click handler 2!")
end

-- This button demonstrates the difficulty in disconnecting a signal:
local button_2 = Gtk.Button.new_with_label("I can be clicked once!")

local handler_id
-- using widget.<signal>:connect from https://github.com/lgi-devs/lgi/blob/master/docs/guide.md#341-connecting-signals
handler_id = button_2.on_clicked:connect(function(widget)
    print("Button 1 Clicked!")
    widget:set_label("I can no longer be clicked!")

    -- Now I can disconnect nicely!
    button_2.on_clicked:disconnect(handler_id)
end)

local window = Gtk.Window {
    title = "GitHub issue demo",
    default_width = 400,
    default_height = 300,
    on_destroy = Gtk.main_quit,
    child = Gtk.VBox {
        button_1,
        button_2
    },
}

window:present()
window:show_all()

Gtk.main()
