------------------------------------------------------------------------------
--
--  LGI Lua-side lua5 core module type definition compiler
--
--  Copyright (c) 2012 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local type, error, assert, load, setmetatable, pairs, ipairs,
tostring
   = type, error, assert, loadstring or load, setmetatable, pairs, ipairs,
   tostring
local table = require 'table'

local core = require 'lgi.core.lua5.lua5'

-- Prepare debugging tables, if debugging is enabled.
local lgi_debug = _G.lgi_debug
if lgi_debug then
   lgi_debug.closures = setmetatable({}, { __mode = "k" })
end

local compiler = {}

-- Definition of bytecode constants, keep in sync with ctype.c
local bc = {
   VOID     = 0x00,
   BOOLEAN  = 0x01,
   INT      = 0x02,
   UINT     = 0x03,
   FLOAT    = 0x04,
   GTYPE    = 0x05,
   STRING   = 0x06,
   COMPOUND = 0x07,
   ENUM     = 0x08,
   ARRAY    = 0x09,
   LIST     = 0x0a,
   HASH     = 0x0b,
   CARRAY   = 0x0c,
   CALLABLE = 0x0d,

   VARIANT_MULT  = 0x10,
   TRANSFER      = 0x40,
   OPTIONAL      = 0x80,
   POINTER       = 0x100,

   VARIANT_INT_8           = 0x00,
   VARIANT_INT_16          = 0x10,
   VARIANT_INT_32          = 0x20,
   VARIANT_INT_64          = 0x30,
   VARIANT_FLOAT_FLOAT     = 0x00,
   VARIANT_FLOAT_DOUBLE    = 0x10,
   VARIANT_STRING_UTF8     = 0x00,
   VARIANT_STRING_FILENAME = 0x10,
   VARIANT_ARRAY_ARRAY     = 0x00,
   VARIANT_ARRAY_PTRARRAY  = 0x10,
   VARIANT_ARRAY_BYTEARRAY = 0x20,
   VARIANT_ARRAY_FIXEDC    = 0x30,
   VARIANT_LIST_SLIST      = 0x00,
   VARIANT_LIST_LIST       = 0x10,
}

-- Table of type handlers, indexed by type mnemonic name
local typehandler = {}

local function get_code(code, params)
   if params.optional then code = code + bc.OPTIONAL end
   if params.transfer then code = code + bc.TRANSFER end
   return code
end

function typehandler.void(context, typename, params)
   context.ti[#context.ti + 1] = bc.void
   return 'void'
end

function typehandler.string(context, typename, params)
   -- Prepare bytecode
   local code = get_code(bc.STRING + bc.POINTER, params)
   if params.filename then
      code = code + bc.VARIANT_STRING_FILENAME
   end
   context.ti[#context.ti + 1] = code
   return 'pointer'
end

-- Adds compiled type info 'source' into 'target' table.
local function compile_type(context, index, source)
   -- Find type handler for this type.
   local typename, params
   if type(source) == 'string' then
      typename = source
      params = {}
   else
      assert(type(source) == 'table' or type(source[1]) == 'string',
	     "bad type")
      typename = source[1]
      params = source
   end
   if type(typename) ~= 'string' then return false end
   local handler = typehandler[typename]
   assert(handler, "bad type")

   -- Invoke typehandler.
   local ffitype = handler(context, typename, params)
   assert(ffitype, "bad type")

   -- If we have output argument, ffidef is actually 'pointer', except
   -- when it is 'return'.
   local dir = params.dir or 'in'
   if #context.def == 0 then
      -- Set up ffidef slots for return value arguments.
      context.def[1] = { ffitype, false, ffitype ~= 'pointer' }
   else
      -- Set up ffidef slots for C function argument.
      if dir == 'out' or dir == 'inout' then ffitype = 'pointer' end
      local def = context.def[index] or {}
      context.def[index] = def
      def[1] = ffitype
      def[2] = (dir == 'in' or dir == 'inout')
      def[3] = (dir == 'out' or dir == 'inout' or dir == 'out-caller-alloc')
   end
end

function compiler.gate(source, target, direction)
   -- Prepare context table holding everything needed to compile
   -- function definition.
   local context = {
      ti = {},
      def = {},
      guard = 0,
   }

   -- Process all type arguments, starting with return value.
   compile_type(context, 1, source.ret or 'void')
   for i, def in ipairs(source) do compile_type(context, i + 1, def) end

   -- Replace bool placeholders in input/output def markers with real
   -- input/output numbers.
   local argnum = { 0, 0 }
   for i, def in ipairs(context.def) do
      for j = 1, 2 do
	 if def[j + 1] then
	    argnum[j] = argnum[j] + 1
	    def[j + 1] = argnum[j]
	 end
      end
   end

   -- Create proper call_info instance.
   local call_info = core.call.new(context.def, context.ti, context.guard)

   -- Start creating source of closure wrapper to JIT.
   local rawname = 'raw_' .. (source.name or 'anonymous'):gsub('[^_%w]', '_')
   local src = {
      'local core, call_info, target, guard_size = ...\n',
      'local function ', rawname, '(',
   }

   -- Create the list of input arguments.
   local args = {}
   for i, def in ipairs(context.def) do
      if not def.internal and def[2] then
	 args[#args + 1] = 'i' .. tostring(i)
      end
   end
   src[#src + 1] = table.concat(args, ', ')
   src[#src + 1] = ')\n'

   -- Finish the definition.
   src[#src + 1] = 'end\nreturn ' .. rawname
   local src = table.concat(src)

   -- Finally compile and execute chunk which return the closure.
   local closure = assert(load(src))(core, call_info, target, context.guard)
   if lgi_debug then
      lgi_debug.closures[closure] = { source = src, context = context }
   end
   return closure
end

function compiler.field(def)
end

return compiler
