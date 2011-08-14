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
stage.color = Clutter.Color(0, 0, 0, 255)
stage.width = 512
stage.height = 512
stage.title = 'LGI Clutter Demo'

local rects = {}
for i = 1, 6 do
   rects[i] = Clutter.Rectangle {
      color = Clutter.Color(
	 256 / 6 * ((i - 1) % 6),
	 256 / 6 * ((i + 3) % 6),
	 256 / 6 * ((-i + 8) % 6),
	 128),
      width = 100, height = 100,
      fixed_x = 200, fixed_y = 200,
      anchor_x = 128, anchor_y = 64,
      reactive = true,
      on_button_press_event = function(rect) rect:raise_top() return true end,
   }
   stage:add_actor(rects[i])
   rects[i]:show()
end

local timeline = Clutter.Timeline { duration = 60, loop = true }
local rotation, rotation_delta = 0, 0.01
local scale, scale_delta = 1, 0.001
function timeline:on_new_frame(frame_num)
   rotation = rotation + rotation_delta
   scale = scale + scale_delta
   if scale > 2 or scale < 1 then scale_delta = -scale_delta end
   for i = 1, #rects do
      rects[i]:set_rotation(Clutter.RotateAxis.Z_AXIS, rotation * (#rects - i),
			    0, 0, 0)
      rects[i]:set_scale(scale, 3 - scale)
   end

   -- A bug in clutter?  If following line is not present, stage stops
   -- redrawing itself after a while...
   stage:queue_redraw()
end

function stage:on_button_press_event(event)
   app:release()
   return true
end

function app:on_activate()
   self:hold()
   stage:show()
   timeline:start()
end

return app:run { arg[0], ... }
