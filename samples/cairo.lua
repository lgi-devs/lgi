#! /usr/bin/env lua

--
-- Sample cairo application, based on http://cairographics.org/samples/
--
-- Renders all samples into separate PNG images
--

local math = require 'math'
local lgi = require 'lgi'
local cairo = lgi.cairo

local dir = arg[0]:sub(1, arg[0]:find('[^%/\\]+$') - 1):gsub('[/\\]$', '')
local imagename = dir .. '/gtk-demo/apple-red.png'

local samples = {}

function samples.arc(cr)
   local xc, yc = 128, 128
   local radius = 100
   local angle1, angle2 = math.rad(45), math.rad(180)

   cr.line_width = 10
   cr:arc(xc, yc, radius, angle1, angle2)
   cr:stroke()

   -- draw helping lines
   cr:set_source_rgba(1, 0.2, 0.2, 0.6)
   cr.line_width = 6

   cr:arc(xc, yc, 10, 0, math.rad(360))
   cr:fill()

   cr:arc(xc, yc, radius, angle1, angle1)
   cr:line_to(xc, yc)
   cr:arc(xc, yc, radius, angle2, angle2)
   cr:line_to(xc, yc)
   cr:stroke()
end

function samples.arc_negative(cr)
   local xc, yc = 128, 128
   local radius = 100
   local angle1, angle2 = math.rad(45), math.rad(180)

   cr.line_width = 10
   cr:arc_negative(xc, yc, radius, angle1, angle2)
   cr:stroke()

   -- draw helping lines
   cr:set_source_rgba(1, 0.2, 0.2, 0.6)
   cr.line_width = 6

   cr:arc(xc, yc, 10, 0, math.rad(360))
   cr:fill()

   cr:arc(xc, yc, radius, angle1, angle1)
   cr:line_to(xc, yc)
   cr:arc(xc, yc, radius, angle2, angle2)
   cr:line_to(xc, yc)
   cr:stroke()
end

function samples.clip(cr)
   cr:arc(128, 128, 76.8, 0, math.rad(360))
   cr:clip()

   -- current path is not consumed by cairo.Context.clip()
   cr:new_path()

   cr:rectangle(0, 0, 256, 256)
   cr:fill()
   cr:set_source_rgb(0, 1, 0)
   cr:move_to(0, 0)
   cr:line_to(256, 256)
   cr:move_to(256, 0)
   cr:line_to(0, 256)
   cr.line_width = 10
   cr:stroke()
end

function samples.clip_image(cr)
   cr:arc(128, 128, 76.8, 0, math.rad(360))
   cr:clip()
   cr:new_path()

   local image = cairo.ImageSurface.create_from_png(imagename)
   cr:scale(256 / image.width, 256 / image.height)
   cr:set_source_surface(image, 0, 0)
   cr:paint()
end

function samples.dash(cr)
   cr:set_dash({ 50, 10, 10, 10 }, -50)

   cr.line_width = 10
   cr:move_to(128, 25.6)
   cr:line_to(230.4, 230.4)
   cr:rel_line_to(-102.4, 0)
   cr:curve_to(51.2, 230.4, 51.2, 128, 128, 128)

   cr:stroke()
end

function samples.curve_to(cr)
   local x, y = 25.6, 128
   local x1, y1 = 102.4, 230.4
   local x2, y2 = 153.6, 25.6
   local x3, y3 = 230.4, 128

   cr:move_to(x, y)
   cr:curve_to(x1, y1, x2, y2, x3, y3)

   cr.line_width = 10
   cr:stroke()

   cr:set_source_rgba(1, 0.2, 0.2, 0.6)
   cr.line_width = 6
   cr:move_to(x, y)
   cr:line_to(x1, y1)
   cr:move_to(x2, y2)
   cr:line_to(x3, y3)
   cr:stroke()
end

function samples.fill_and_stroke(cr)
   cr:move_to(128, 25.6)
   cr:line_to(230.4, 230.4)
   cr:rel_line_to(-102.4, 0)
   cr:curve_to(51.2, 230.4, 51.2, 128, 128, 128)
   cr:close_path()

   cr:move_to(64, 25.6)
   cr:rel_line_to(51.2, 51.2)
   cr:rel_line_to(-51.2, 51.2)
   cr:rel_line_to(-51.2, -51.2)
   cr:close_path()

   cr.line_width = 10
   cr:set_source_rgb(0, 0, 1)
   cr:fill_preserve()
   cr:set_source_rgb(0, 0, 0)
   cr:stroke()
end

function samples.fill_style(cr)
   cr.line_width = 6

   cr:rectangle(12, 12, 232, 70)
   cr:new_sub_path()
   cr:arc(64, 64, 40, 0, math.rad(360))
   cr:new_sub_path()
   cr:arc_negative(192, 64, 40, 0, math.rad(-360))

   cr.fill_rule = 'EVEN_ODD'
   cr:set_source_rgb(0, 0.7, 0)
   cr:fill_preserve()
   cr:set_source_rgb(0, 0, 0)
   cr:stroke()

   cr:translate(0, 128)
   cr:rectangle(12, 12, 232, 70)
   cr:new_sub_path()
   cr:arc(64, 64, 40, 0, math.rad(360))
   cr:new_sub_path()
   cr:arc_negative(192, 64, 40, 0, math.rad(-360))

   cr.fill_rule = 'WINDING'
   cr:set_source_rgb(0, 0, 0.9)
   cr:fill_preserve()
   cr:set_source_rgb(0, 0, 0)
   cr:stroke()
end

function samples.gradient(cr)
   local pat = cairo.Pattern.create_linear(0, 0, 0, 256)
   pat:add_color_stop_rgba(1, 0, 0, 0, 1)
   pat:add_color_stop_rgba(0, 1, 1, 1, 1)
   cr:rectangle(0, 0, 256, 256)
   cr.source = pat
   cr:fill()

   pat = cairo.Pattern.create_radial(115.2, 102.4, 25.6,
				     102.4, 102.4, 128)
   pat:add_color_stop_rgba(0, 1, 1, 1, 1);
   pat:add_color_stop_rgba(1, 0, 0, 0, 0);
   cr.source = pat
   cr:arc(128, 128, 76.8, 0, math.rad(360))
   cr:fill()
end

function samples.image(cr)
   local image = cairo.ImageSurface.create_from_png(imagename)
   
   cr:translate(128, 128)
   cr:rotate(math.rad(45))
   cr:scale(256 / image.width, 256 / image.height)
   cr:translate(-image.width / 2, -image.height / 2)

   cr:set_source_surface(image, 0, 0)
   cr:paint()
end

function samples.imagepattern(cr)
   local image = cairo.ImageSurface.create_from_png(imagename)

   local pattern = cairo.Pattern.create_for_surface(image)
   pattern.extend = 'REPEAT'

   cr:translate(128, 128)
   cr:rotate(math.rad(45))
   cr:scale(1 / math.sqrt(2), 1 / math.sqrt(2))
   cr:translate(-128, -128)

   pattern.matrix = cairo.Matrix.create_scale(image.width / 256 * 5,
					      image.height / 256 * 5)
   cr.source = pattern

   cr:rectangle(0, 0, 256, 256)
   cr:fill()
end

function samples.multisegment_caps(cr)
   cr:move_to(50, 75)
   cr:line_to(200, 75)

   cr:move_to(50, 125)
   cr:line_to(200, 125)

   cr:move_to(50, 175)
   cr:line_to(200, 175)

   cr.line_width = 30
   cr.line_cap = 'ROUND'
   cr:stroke()
end

function samples.rounded_rectangle(cr)
   local x, y, width, height = 25.6, 25.6, 204.8, 204.8
   local aspect = 1
   local corner_radius = height / 10

   local radius = corner_radius / aspect

   cr:new_sub_path()
   cr:arc(x + width - radius, y + radius, radius,
	  math.rad(-90), math.rad(0))
   cr:arc(x + width - radius, y + height - radius, radius,
	  math.rad(0), math.rad(90))
   cr:arc(x + radius, y + height - radius, radius,
	  math.rad(90), math.rad(180))
   cr:arc(x + radius, y + radius, radius,
	  math.rad(180), math.rad(270))
   cr:close_path()

   cr:set_source_rgb(0.5, 0.5, 1)
   cr:fill_preserve()
   cr:set_source_rgba(0.5, 0, 0, 0.5)
   cr.line_width = 10
   cr:stroke()
end

-- Iterate through all samples and create .png files from them
for name, sample in pairs(samples) do
   local surface = cairo.ImageSurface.create('ARGB32', 256, 256)
   local cr = cairo.Context.create(surface)
   sample(cr)
   surface:write_to_png('cairodemo-' .. name .. '.png')
end
