
-- You should call this script
-- in the following manner:
--
-- Gtk 3:
--    lua "path\to\this\script.lua" 3
-- 
-- Gtk 4:
--    lua "path\to\this\script.lua" 4
--
-- Gtk 3:
--    luajit "path\to\this\script.lua" 3
-- 
-- Gtk 4:
--    luajit "path\to\this\script.lua" 4
--

local gtk_major_version = assert(tonumber(arg[1]))

if (not (gtk_major_version == 3 or gtk_major_version == 4)) then
    error("unknown Gtk major version")
end

local gtk_version = ("%d.0"):format(gtk_major_version)

local lgi = assert(require("lgi"))
local Gtk = assert(lgi.require("Gtk", gtk_version))

local app = Gtk.Application({ application_id = "org.lgi-devs.lgi" })

function app:on_activate()
    local w = Gtk.ApplicationWindow()
    w:set_default_size(900, 600)
    w:set_title("My great title")

    w.application = self

    if (gtk_major_version == 3) then
        w:show_all()
    elseif (gtk_major_version == 4) then
        w:present()
    else
        error("Unknown GTK version")
    end

    w:close()
end

app:run()