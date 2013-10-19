-------------------------------------------------------------------------------
--
-- LGI GLib MarkupParser support implementation.
--
-- Copyright (c) 2013 Pavel Holejsovsky
-- Licensed under the MIT license:
-- http://www.opensource.org/licenses/mit-license.php
--
-------------------------------------------------------------------------------

local pairs, ipairs, setmetatable, select =
   pairs, ipairs, setmetatable, select

local lgi = require 'lgi'
local core = require 'lgi.core'
local record = require 'lgi.record'
local component = require 'lgi.component'
local ffi = require 'lgi.ffi'
local ti = ffi.types

local MarkupParser = lgi.GLib.MarkupParser
local MarkupParseContext = lgi.GLib.MarkupParseContext

local parser_guard = setmetatable({}, { __mode = 'k' })
local parser_field = MarkupParser._field
MarkupParser._field = nil
MarkupParser._attribute = {}
MarkupParser._allow = true

-- Replace fields with function pointers with attributes which actually
-- convert Lua target into C callback.
for name, def in pairs {
   start_element = {
      signature = {
	 name = 'start_element', throws = true, ret = ti.void,
	 MarkupParseContext, ti.utf8, ti.GStrv, ti.GStrv, ti.ptr
      },
      override = function(target)
	 return function(context, element, attr_names, attr_values, user_data)
	    -- Extend attr_names with name=value dictionary part.
	    for i = 1, #attr_names do
	       attr_names[attr_names[i]] = attr_values[i]
	    end
	    (function(...)
		if select('#', ...) == 1 then
		   error(select(1, ...), 0)
		elseif select('#', ...) > 1 and not select(1, ...) then
		   error(select(2, ...), 0)
		end
	    end)(target(context, element, attr_names, attr_values, user_data))
	 end
      end,
   },
   end_element = {},
   passthrough = {},
   text = {},
   error = {},
} do
   MarkupParser._attribute[name] = { set = function(parser, target)
      -- Prepare guards table for this parser
      local guards = parser_guard[parser]
      if not guards then
	 guards = {}
	 parser_guard[parser] = guards
      end

      -- Generate real function pointer and guard for the target
      local cbk_type = def.signature or parser_field[name].typeinfo.interface
      if def.override then
	 target = def.override(target)
      end
      local guard, funcptr = core.marshal.callback(cbk_type, target)
      guards[name] = guard
      core.record.field(parser, parser_field[name], funcptr)
   end }
end

-- ParseContext helper overrides.
function MarkupParseContext.new(parser, flags, user_data)
   -- DestroyNotify is required (allow-none) annotation is missing, so
   -- provide dummy one.
   return MarkupParseContext._method.new(parser, flags, user_data,
					 function() end)
end
function MarkupParseContext._new(typetable, parser, flags)
   return MarkupParseContext.new(parser, flags)
end

function MarkupParseContext:parse(text, len)
   return MarkupParseContext._method.parse(self,  text, len or -1)
end

MarkupParseContext._method.pop = core.callable.new {
   name = 'GLib.MarkupParseContext.pop',
   addr = core.gi.GLib.resolve.g_markup_parse_context_pop,
   ret = ti.ptr, MarkupParseContext
}

local escape_text = lgi.GLib.markup_escape_text
function lgi.GLib.markup_escape_text(text)
   return escape_text(text, -1)
end
