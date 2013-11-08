#! /usr/bin/env lua

--
-- Sample GMarkupParser which parses input into the table. Expects
-- name of the markup file on the commandline.
--

local io = require 'io'
local lgi = require 'lgi'
local GLib = lgi.GLib

local assert = lgi.assert

local function make_parser(doc)
   local stack = { doc }
   local parser = GLib.MarkupParser {
      start_element = function(context, tag, attr)
	 local element = { tag = tag, attr = attr }
	 stack[#stack + 1] = element
      end,

      text = function(context, text)
	 -- Avoid just whitespace.
	 if text:match('%S') then
	    local element = stack[#stack]
	    element[#element + 1] = text
	 end
      end,

      end_element = function(context)
	 local parent = stack[#stack - 1]
	 parent[#parent + 1] = stack[#stack]
	 stack[#stack] = nil
      end,
   }
   return parser
end

local function dump_element(result, element, indent)
   result[#result + 1] = ('%s<%s'):format(indent, element.tag)
   for name, value in pairs(element.attr) do
      if type(name) == 'string' then
	 result[#result + 1] = (' %s=\'%s\''):format(
	    name, GLib.markup_escape_text(value))
      end
   end
   if #element == 0 then
      result[#result + 1] = '/>\n'
   else
      result[#result + 1] = '>\n'
      for i = 1, #element do
	 if type(element[i]) == 'table' then
	    dump_element(result, element[i], indent .. ' ')
	 else
	    result[#result + 1] = GLib.markup_escape_text(element[i])
	    result[#result + 1] = '\n'
	 end
      end
      result[#result + 1] = ('%s</%s>\n'):format(indent, element.tag)
   end
end

local function dump_doc(doc)
   local buffer = {}
   dump_element(buffer, doc[1], '')
   return table.concat(buffer)
end

-- Parse standard input.
local document = {}
local parser = make_parser(document)
local context = GLib.MarkupParseContext(parser, 'TREAT_CDATA_AS_TEXT')
for line in io.lines((...), 512) do
   assert(context:parse(line))
end
context:end_parse()
print(dump_doc(document))
