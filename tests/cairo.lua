--[[--------------------------------------------------------------------------

  LGI testsuite, cairo overrides test group.

  Copyright (c) 2012 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local io = require 'io'
local os = require 'os'
local lgi = require 'lgi'

local check = testsuite.check
local checkv = testsuite.checkv
local cairo = testsuite.group.new('cairo')

function cairo.status()
   local cairo = lgi.cairo

   for name, value in pairs(cairo.Status) do
      if type(name) == 'string' and type(value) == 'number' then
	 checkv(cairo.Status.to_string(name),
		cairo.Status.to_string(value),
		'string')
      end
   end
end

local function check_matrix(matrix, xx, yx, xy, yy, x0, y0)
   checkv(matrix.xx, xx, 'number')
   checkv(matrix.yx, yx, 'number')
   checkv(matrix.xy, xy, 'number')
   checkv(matrix.yy, yy, 'number')
   checkv(matrix.x0, x0, 'number')
   checkv(matrix.y0, y0, 'number')
end

function cairo.matrix()
   local cairo = lgi.cairo

   local matrix = cairo.Matrix()
   check_matrix(matrix, 0, 0, 0, 0, 0, 0)

   matrix = cairo.Matrix { xx = 1, yx =1.5,
			   xy = 2, yy = 2.5,
			   x0 = 3, y0 = 3.5 }
   check_matrix(matrix, 1, 1.5, 2, 2.5, 3, 3.5)
end

function cairo.matrix_getset()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   local m = cairo.Matrix { xx = 1, yx =1.5,
			    xy = 2, yy = 2.5,
			    x0 = 3, y0 = 3.5 }
   cr.matrix = m
   local m2 = cr.matrix
   check(m.xx == m2.xx)
   check(m.yx == m2.yx)
   check(m.xy == m2.xy)
   check(m.yy == m2.yy)
   check(m.x0 == m2.x0)
   check(m.y0 == m2.y0)
end

function cairo.matrix_init()
   local cairo = lgi.cairo

   local m = cairo.Matrix.create_identity()
   check_matrix(m, 1, 0, 0, 1, 0, 0)

   local m = cairo.Matrix.create_translate(2, 3)
   check_matrix(m, 1, 0, 0, 1, 2, 3)

   local m = cairo.Matrix.create_scale(2, 3)
   check_matrix(m, 2, 0, 0, 3, 0, 0)

   local angle = math.pi / 2
   local m = cairo.Matrix.create_rotate(angle)
   local c, s = math.cos(angle), math.sin(angle)
   check_matrix(m, c, s, -s, c, 0, 0)
end

function cairo.matrix_operations()
   local cairo = lgi.cairo
   local m = cairo.Matrix.create_identity()

   m:translate(2, 3)
   check_matrix(m, 1, 0, 0, 1, 2, 3)

   m:scale(-2, -3)
   check_matrix(m, -2, 0, 0, -3, 2, 3)

   m:rotate(0)
   check_matrix(m, -2, 0, 0, -3, 2, 3)

   local m2 = cairo.Matrix.create_translate(2, 3)
   local status = m2:invert()
   checkv(status, 'SUCCESS', 'string')
   check_matrix(m2, 1, 0, 0, 1, -2, -3)

   -- XXX: This API could be improved
   local result = cairo.Matrix.create_identity()
   result:multiply(m, m2)
   check_matrix(result, -2, 0, 0, -3, 0, 0)

   local x, y = m:transform_point(1, 1)
   checkv(x, 0, 'number')
   checkv(y, 0, 'number')

   local x, y = m:transform_distance(1, 1)
   checkv(x, -2, 'number')
   checkv(y, -3, 'number')
end

function cairo.dash()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   local dash, offset = cr:get_dash()
   check(type(dash) == 'table')
   check(next(dash) == nil)

   cr:set_dash({ 1, 2, math.pi }, 2.22)
   dash, offset = cr:get_dash()
   check(#dash == 3)
   check(dash[1] == 1)
   check(dash[2] == 2)
   check(dash[3] == math.pi)
   check(offset == 2.22)

   cr:set_dash(nil, 0)
   dash, offset = cr:get_dash()
   check(type(dash) == 'table')
   check(next(dash) == nil)
end

function cairo.path()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   cr:move_to(10, 11)
   cr:curve_to(1, 2, 3, 4, 5, 6)
   cr:close_path()
   cr:line_to(21, 22)

   local i = 1
   for t, pts in cr:copy_path():pairs() do
      if i == 1 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 10, 'number')
	 checkv(pts[1].y, 11, 'number')
      elseif i == 2 then
	 checkv(t, 'CURVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 3)
	 checkv(pts[1].x, 1, 'number')
	 checkv(pts[1].y, 2, 'number')
	 checkv(pts[2].x, 3, 'number')
	 checkv(pts[2].y, 4, 'number')
	 checkv(pts[3].x, 5, 'number')
	 checkv(pts[3].y, 6, 'number')
      elseif i == 3 then
	 checkv(t, 'CLOSE_PATH', 'string')
	 check(type(pts) == 'table' and #pts == 0)
      elseif i == 4 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 10, 'number')
	 checkv(pts[1].y, 11, 'number')
      elseif i == 5 then
	 checkv(t, 'LINE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 21, 'number')
	 checkv(pts[1].y, 22, 'number')
      else
	 check(false)
      end
      i = i + 1
   end
   check(i == 6)
end

function cairo.surface_type()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   check(cairo.ImageSurface:is_type_of(surface))
   check(cairo.Surface:is_type_of(surface))
   check(not cairo.RecordingSurface:is_type_of(surface))

   local s2 = cr.target
   check(cairo.ImageSurface:is_type_of(s2))
   check(cairo.Surface:is_type_of(s2))
   check(not cairo.RecordingSurface:is_type_of(s2))

   local s3 = cr.group_target
   check(cairo.ImageSurface:is_type_of(s3))
   check(cairo.Surface:is_type_of(s3))
   check(not cairo.RecordingSurface:is_type_of(s3))
end

function cairo.pattern_type()
   local cairo = lgi.cairo
   local pattern

   pattern = cairo.Pattern.create_rgb(1, 1, 1)
   check(cairo.SolidPattern:is_type_of(pattern))
   check(cairo.Pattern:is_type_of(pattern))
   pattern = cairo.SolidPattern(1, 1, 1)
   check(cairo.SolidPattern:is_type_of(pattern))
   pattern = cairo.SolidPattern(1, 1, 1, 1)
   check(cairo.SolidPattern:is_type_of(pattern))

   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   pattern = cairo.Pattern.create_for_surface(surface)
   check(select(2, pattern:get_surface()) == surface)
   check(cairo.SurfacePattern:is_type_of(pattern))
   check(cairo.Pattern:is_type_of(pattern))
   pattern = cairo.SurfacePattern(surface)
   check(cairo.SurfacePattern:is_type_of(pattern))

   pattern = cairo.Pattern.create_linear(0, 0, 10, 10)
   check(cairo.LinearPattern:is_type_of(pattern))
   check(cairo.GradientPattern:is_type_of(pattern))
   check(cairo.Pattern:is_type_of(pattern))
   pattern = cairo.LinearPattern(0, 0, 10, 10)
   check(cairo.LinearPattern:is_type_of(pattern))

   pattern = cairo.Pattern.create_radial(0, 0, 5, 10, 10, 5)
   check(cairo.RadialPattern:is_type_of(pattern))
   check(cairo.GradientPattern:is_type_of(pattern))
   check(cairo.Pattern:is_type_of(pattern))
   pattern = cairo.RadialPattern(0, 0, 5, 10, 10, 5)
   check(cairo.RadialPattern:is_type_of(pattern))

   if cairo.version >= cairo.version_encode(1, 12, 0) then
      pattern = cairo.Pattern.create_mesh()
      check(cairo.MeshPattern:is_type_of(pattern))
      check(not cairo.GradientPattern:is_type_of(pattern))
      check(cairo.Pattern:is_type_of(pattern))
      pattern = cairo.MeshPattern()
      check(cairo.MeshPattern:is_type_of(pattern))
   end
end

function cairo.pattern_mesh()
   local cairo = lgi.cairo

   -- Mesh patterns are introduced in cairo 1.12
   if cairo.version < cairo.version_encode(1, 12, 0) then
      return
   end

   local mesh = cairo.Pattern.create_mesh()
   local pattern = cairo.Pattern.create_radial(1, 2, 3, 4, 5, 6)

   check(cairo.Pattern:is_type_of(mesh))
   check(cairo.MeshPattern:is_type_of(mesh))

   check(cairo.Pattern:is_type_of(pattern))
   check(not cairo.MeshPattern:is_type_of(pattern))

   local function check_status(status)
       checkv(status, 'SUCCESS', 'string')
   end

   -- Taken from cairo's pattern-getters test and slightly adapted to use all
   -- functions of the mesh pattern API
   local status, count = mesh:get_patch_count()
   check_status(status)
   checkv(count, 0, 'number')

   mesh:begin_patch()
   mesh:move_to(0, 0)
   mesh:line_to(0, 3)
   mesh:line_to(3, 3)
   mesh:line_to(3, 0)
   mesh:set_corner_color_rgba(0, 1, 1, 1, 1)
   mesh:end_patch()

   local status, count = mesh:get_patch_count()
   check_status(status)
   checkv(count, 1, 'number')

   for k, v in pairs({ { 1, 1 }, { 1, 2 }, { 2, 2 }, { 2, 1 } }) do
       local status, x, y = mesh:get_control_point(0, k - 1)
       check_status(status)
       checkv(x, v[1], 'number')
       checkv(y, v[2], 'number')
   end

   mesh:begin_patch()
   mesh:move_to(0, 0)
   mesh:line_to(1, 0)
   mesh:curve_to(1, 1, 1, 2, 0, 1)
   mesh:set_corner_color_rgb(0, 1, 1, 1)
   mesh:set_control_point(2, 0.5, 0.5)
   mesh:end_patch()

   local status, count = mesh:get_patch_count()
   check_status(status)
   checkv(count, 2, 'number')

   for k, v in pairs({ 1, 0, 0, 1 }) do
       local status, r, g, b, a = mesh:get_corner_color_rgba(1, k - 1)
       check_status(status)
       checkv(r, v, 'number')
       checkv(g, v, 'number')
       checkv(b, v, 'number')
       checkv(a, v, 'number')
   end

   local i = 0
   local expected = {
      { { 0, 1 }, { 0, 2 }, { 0, 3 } },
      { { 1, 3 }, { 2, 3 }, { 3, 3 } },
      { { 3, 2 }, { 3, 1 }, { 3, 0 } },
      { { 2, 0 }, { 1, 0 }, { 0, 0 } },
   }
   for t, pts in mesh:get_path(0):pairs() do
      if i == 0 then
	 checkv(t, 'MOVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 1)
	 checkv(pts[1].x, 0, 'number')
	 checkv(pts[1].y, 0, 'number')
      else
	 -- Mesh patterns turn everything into curves. :-(
	 checkv(t, 'CURVE_TO', 'string')
	 check(type(pts) == 'table' and #pts == 3)
	 for k, v in pairs(expected[i]) do
	    checkv(pts[k].x, v[1], 'number')
	    checkv(pts[k].y, v[2], 'number')
	 end
      end
      i = i + 1
   end
   check(i == #expected + 1)
end

function cairo.context_getset()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   local s2 = cr.target
   check(s2 == surface)

   local s3 = cr.group_target
   check(s3 == surface)

   cr.source = cairo.Pattern.create_linear(0, 0, 10, 10)
   check(cairo.LinearPattern:is_type_of(cr.source))

   cr.antialias = "BEST"
   check(cr.antialias == "BEST")

   cr.fill_rule = "EVEN_ODD"
   check(cr.fill_rule == "EVEN_ODD")

   cr.line_cap = "SQUARE"
   check(cr.line_cap == "SQUARE")

   cr.line_join = "BEVEL"
   check(cr.line_join == "BEVEL")

   cr.line_width = 42
   check(cr.line_width == 42)

   cr.miter_limit = 5
   check(cr.miter_limit == 5)

   cr.operator = "ATOP"
   check(cr.operator == "ATOP")

   cr.tolerance = 21
   check(cr.tolerance == 21)

   local m = cairo.Matrix.create_translate(-1, 4)
   cr.matrix = m
   check_matrix(cr.matrix, 1, 0, 0, 1, -1, 4)

   local m = cairo.Matrix.create_scale(2, 3)
   cr.font_matrix = m
   check_matrix(cr.font_matrix, 2, 0, 0, 3, 0, 0)

   -- font size is read-only, but messes with the font matrix
   cr.font_size = 100
   check_matrix(cr.font_matrix, 100, 0, 0, 100, 0, 0)

   local opt = cairo.FontOptions.create()
   cr.font_options = opt
   check(cr.font_options:equal(opt))

   local font_face = cairo.ToyFontFace.create("Arial", cairo.FontSlant.NORMAL, cairo.FontWeight.BOLD)
   cr.font_face = font_face
   check(cairo.ToyFontFace:is_type_of(cr.font_face))

   local scaled_font = cairo.ScaledFont.create(font_face, m, m, opt)
   cr.scaled_font = scaled_font
   check(cairo.ScaledFont:is_type_of(cr.scaled_font))
end

function cairo.context_transform()
   local cairo = lgi.cairo
   local surface = cairo.ImageSurface('ARGB32', 100, 100)
   local cr = cairo.Context(surface)

   function compare(a, b)
      check(math.abs(a-b) < 0.1)
   end

   cr:rotate(-math.pi / 2)
   cr:translate(100, 200)

   local x, y = cr:user_to_device(10, 20)
   compare(x, 220)
   compare(y, -110)

   local x, y = cr:device_to_user(220, -110)
   compare(x, 10)
   compare(y, 20)

   local x, y = cr:user_to_device_distance(10, 20)
   compare(x, 20)
   compare(y, -10)

   local x, y = cr:device_to_user_distance(20, -10)
   compare(x, 10)
   compare(y, 20)
end
