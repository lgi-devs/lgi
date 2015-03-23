------------------------------------------------------------------------------
--
--  LGI Performance test module
--
--  Copyright (c) 2013 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local lgi = require("lgi")
local cairo = lgi.cairo
local Gtk = lgi.Gtk
local GLib = lgi.GLib

local width, height = 200, 200
local surf = cairo.ImageSurface('ARGB32', width, height)
local cr = cairo.Context(surf)
local w = Gtk.Window()
local cairo_move_to = cairo.Context.move_to

for _, test in ipairs {
   { 100000, function() cr:move_to(100, 100) end },
   { 100000, function() cairo.Context.move_to(cr, 100, 100) end },
   { 100000, function() cairo_move_to(cr, 100, 100) end },
   { 100000, function() cr.line_width = 1 end },
   { 100000, function() cr:set_line_width(1) end },
   { 10000, function() w:set_title('title') end },
   { 10000, function() Gtk.Window.set_title(w, 'title') end },
   { 10000, function() w.title = 'title' end },
} do
   local results = {}
   local timer = GLib.Timer()
   for i = 1, test[1] do
      test[2]()
   end
   timer:stop()
   io.write(string.format('%0.2f', timer:elapsed()))
   io.write('\t')
   io.flush()
end
print('\n')

--[[
*** 0.7.2:
1.43	0.82	0.09	1.46	1.40	4.27	1.00	4.85

*** Remove and inline _access_element
1.19	0.83	0.09	1.19	1.15	3.35	1.04	3.84

*** Add caching of methods into main type table or _cached subtable
0.65	0.10	0.10	0.93	0.62	1.62	0.03	2.78

*** Add support for automatic async_ invocation
0.65	0.11	0.10	0.97	0.64	1.72	0.03	2.88

*** 0.8.0: (On Lua5.2)
0.65	0.10	0.10	0.93	0.62	1.62	0.03	2.78

*** 0.9.0: (On Lua5.2)
0.65	0.09	0.09	0.96	0.63	1.76	0.02	2.94
--]]
