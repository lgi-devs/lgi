--[[--------------------------------------------------------------------------

  lgi testsuite, Pango test suite.

  Copyright (c) 2013 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local core = require 'lgi.core'

local check = testsuite.check

-- Pango overrides testing
local pango = testsuite.group.new('pango')

-- Test originating from https://github.com/pavouk/lgi/issues/68
function pango.glyphstring()
   local Pango = lgi.Pango
   local pal = Pango.AttrList.new();
   pal:insert(Pango.Attribute.language_new(Pango.Language.from_string("he")))
   pal:insert(Pango.Attribute.family_new("Adobe Hebrew"))
   pal:insert(Pango.Attribute.size_new(12))

   local fm = lgi.PangoCairo.FontMap.get_default()
   local pango_context = Pango.FontMap.create_context(fm)
   pango_context:set_language(Pango.Language.from_string("he"))
   local s = "ltr שָׁוְא ltr"

   items = Pango.itemize(pango_context, s, 0, string.len(s), pal, nil)

   for i in pairs(items) do
      local offset = items[i].offset
      local length = items[i].length
      local analysis = items[i].analysis
      local pgs = Pango.GlyphString()
      Pango.shape(string.sub(s,1+offset), length, analysis, pgs)
      -- Pull out individual glyphs with pgs.glyphs
      local glyphs = pgs.glyphs
      check(type(glyphs) == 'table')
      check(#glyphs > 0)
      check(Pango.GlyphInfo:is_type_of(glyphs[1]))
   end
end
