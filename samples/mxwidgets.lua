#! /usr/bin/env lua

--
-- Basic MX sample, adapted from Vala code from
-- http://live.gnome.org/Vala/MxSample
--

local lgi = require('lgi')
local GObject = lgi.GObject
local Mx = lgi.require('Mx', '1.0')
local Clutter = lgi.require('Clutter', '1.0')

local app = Mx.Application { application_name = "MX Widget Factory" }
local window = app:create_window()
window.clutter_stage:set_size(500, 300)

local hbox = Mx.BoxLayout()
window.toolbar:add_actor(hbox)

local button = Mx.Button {
   label = "Click me",
   tooltip_text = "Please click this button!",
   on_clicked = function(self) self.label = "Thank you!" end
}

local combo = Mx.ComboBox()
for _, name in ipairs { "Africa", "Antarctica", "Asia", "Australia", "Europe",
			"North America", "South America" } do
   combo:append_text(name)
end
combo.index = 0
function combo.on_notify:index()
   print(("Selected continent: %s"):format(self.active_text))
end

hbox:add(button, combo)

local table = Mx.Table { column_spacing = 24, row_spacing = 24 }
local button = Mx.Button { label = "Button" }
table:add_actor(button, 0, 0)
table.meta[button].y_fill = false

local entry = Mx.Entry { text = "Entry" }
table:add_actor(entry, 0, 1)
table.meta[entry].y_fill = false

local combo = Mx.ComboBox { active_text = "Combo Box" }
combo:append_text("Hello")
combo:append_text("Dave")
table:add_actor(combo, 0, 2)
table.meta[entry].y_fill = false

local scrollbar = Mx.ScrollBar {
   adjustment = Mx.Adjustment {
      lower = 0, upper = 10,
      page_increment = 1, page_size = 1
   },
   height = 22
}
table:add_actor(scrollbar, 1, 0)
table.meta[entry].y_fill = false

local progressbar = Mx.ProgressBar { progress = 0.7 }
table:add_actor(progressbar, 1, 1)
table.meta[progressbar].y_fill = false

local slider = Mx.Slider()
table:add_actor(slider, 1, 2)
table.meta[slider].y_fill = false
function slider.on_notify:value()
   progressbar.progress = slider.value
end

local pathbar = Mx.PathBar()
for _, path in ipairs { "", "Path", "Bar" } do pathbar:push(path) end
table:add_actor(pathbar, 2, 0)

local expander = Mx.Expander { label = "Expander" }
table:add_actor(expander, 2, 1)
table.meta[expander].y_fill = false
expander:add_actor(Mx.Label { text = "Hello" })

local toggle = Mx.Toggle()
table:add_actor(toggle, 2, 2)
table.meta[toggle].y_fill = false

local togglebutton = Mx.Button { label = "Toggle", is_toggle = true }
table:add_actor(togglebutton, 3, 0)
table.meta[togglebutton].y_fill = false

local checkbutton = Mx.Button { is_toggle = true }
checkbutton:set_style_class('check-box')
table:add_actor(checkbutton, 3, 1)
table.meta[checkbutton].y_fill = false
table.meta[checkbutton].x_fill = false

-- Just for fun, create binding between both kinds of toggles.
togglebutton:bind_property('toggled', checkbutton, 'toggled', 
			   GObject.BindingFlags.BIDIRECTIONAL)

scrollbar = Mx.ScrollBar {
   adjustment = Mx.Adjustment {
      lower = 0, upper = 10,
      page_increment = 1, page_size = 1,
   },
   orientation = Mx.Orientation.VERTICAL,
   width = 22
}
table:add_actor(scrollbar, 0, 3)
table.meta[scrollbar].row_span = 3

window.child = Mx.Frame { child = table }

window.clutter_stage:show()
app:run()
