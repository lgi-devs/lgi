#! /usr/bin/env lua

--
-- Basic clutter demo.
--

local lgi = require('lgi')
local Clutter = lgi.require('Clutter', '1.0')
local GObject = lgi.require('GObject', '2.0')
local Gio = lgi.require('Gio', '2.0')

local app = Gio.Application { application_id = 'org.lgi.samples.Clutter' }

local stage = Clutter.Stage.get_default()
stage.color = Clutter.Color { alpha = 255 }
stage.width = 512
stage.height = 512

local rects = {}
for i = 1, 6 do
   rects[i] = Clutter.Rectangle {
      color = Clutter.Color {
	 red = 256 / 6 * (i - 1),
	 green = 256 / 6 * (6 - i),
	 blue = 256 / 12 * (i - 1),
	 alpha = 128
      },
      width = 100, height = 100,
      fixed_x = 100, fixed_y = 100,
   }
   stage:add_actor(rects[i])
   rects[i]:show()
end

local timeline = Clutter.Timeline { duration = 60, loop = true }
local rotation = 0
function timeline:on_new_frame(frame_num)
   rotation = rotation + 0.3
   for i = 1, #rects do
      rects[i]:set_rotation(Clutter.RotateAxis.Z_AXIS, rotation * (#rects - i),
			    0, 0, 0)
   end
end

function app:on_activate()
   self:hold()
   stage:show()
   timeline:start()
end

return app:run { arg[0], ... }
