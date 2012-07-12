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

local repo = core.repo

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

-- Mapping table for assorted integral type friendly names and nicks,
-- resolving to base integral types.
local integer_types = {
   int8 = 'int8', int16 = 'int16', int32 = 'int32', int64 = 'int64',
   uint8 = 'uint8', uint16 = 'uint16', uint32 = 'uint32', uint64 = 'uint64',

   -- TODO: Make these arch-independent, needs help from C backend
   schar = 'int8', uchar = 'uint8', char = 'int8',
   short = 'int16', ushort = 'uint16',
   int = 'int32', uint = 'uint32',
   long = 'int64', ulong = 'uint64',
}

-- Table of type handlers, indexed by type mnemonic name
local typehandler = {}
local compile_type

local function get_code(code, params)
   if params.opt then code = code + bc.OPTIONAL end
   if params.xfer then code = code + bc.TRANSFER end
   if params.ref then code = code + bc.POINTER end
   return code
end

function typehandler.void(context, typename, params, direction)
   context.ti[#context.ti + 1] = bc.VOID
   return 'void'
end

local typehandler_integer_map = {
   int8 = { bc.INT + bc.VARIANT_INT_8, 'sint8' },
   int16 = { bc.INT + bc.VARIANT_INT_16, 'sint16' },
   int32 = { bc.INT + bc.VARIANT_INT_32, 'sint32' },
   int64 = { bc.INT + bc.VARIANT_INT_64, 'sint64' },
   uint8 = { bc.UINT + bc.VARIANT_INT_8, 'uint8' },
   uint16 = { bc.UINT + bc.VARIANT_INT_16, 'uint16' },
   uint32 = { bc.UINT + bc.VARIANT_INT_32, 'uint32' },
   uint64 = { bc.UINT + bc.VARIANT_INT_64, 'uint64' },
}

-- Generic integers handler
local function typehandler_integer(context, typename, params, direction)
   -- Translate nicks to basic types.
   typename = integer_types[typename]
   local map_entry = assert(typehandler_integer_map[typename])
   context.ti[#context.ti + 1] = get_code(map_entry[1], params)
   return map_entry[2]
end

-- Install all variants of integers handlers.
for name in pairs(integer_types) do
   typehandler[name] = typehandler_integer
end

function typehandler.bool(context, typename, params, direction)
   context.ti[#context.ti + 1] = get_code(bc.BOOLEAN, params)
   return integer_types.uint
end

function typehandler.float(context, typename, params, direction)
   context.ti[#context.ti + 1] = get_code(bc.FLOAT, params)
   return 'float'
end

function typehandler.double(context, typename, params, direction)
   context.ti[#context.ti + 1] = get_code(bc.DOUBLE, params)
   return 'double'
end

function typehandler.string(context, typename, params, direction)
   local code = get_code(bc.STRING + bc.POINTER, params)
   if params.filename then
      code = code + bc.VARIANT_STRING_FILENAME
   end
   context.ti[#context.ti + 1] = code
   return 'pointer'
end

function typehandler.compound(context, typename, params, direction, index)
   -- Compounds as function arguments are always by-ref.
   if index ~= 0 then params.ref = true end

   -- Get bytecode for the compound.
   context.ti[#context.ti + 1] = get_code(bc.COMPOUND, params)

   -- Add the type of the compound to the typeinfo.
   context.ti[#context.ti + 1] = params[2]

   -- If xfer happens, reserve guard slot for it.
   if params.xfer then context.guard = context.guard + 1 end
   return 'pointer'
end

function typehandler.enum(context, typename, params, direction)
   -- We generate bytecode for enum here.  It might be useful to
   -- generate code to convert enum/number on the Lua side and pass
   -- the number instead.
   context.ti[#context.ti + 1] = get_code(bc.ENUM, params)
   context.ti[#context.ti + 1] = params[2]
   local map_entry = assert(typehandler_integer_map[typename])
   return map_entry[2]
end

-- Map of array variant types.
local array_map = {
   array = bc.VARIANT_ARRAY_ARRAY,
   ptr = bc.VARIANT_ARRAY_PTRARRAY,
   byte = bc.VARIANT_ARRAY_BYTEARRAY,
   fixed = bc.VARIANT_ARRAY_FIXEDC,
}

function typehandler.array(context, typename, params, direction, index)
   -- Arrays are always passed as pointers in function arguments.
   if index ~= 0 then params.ref = true end

   -- Resolve the kind of the array.
   if params.fixed then params.type = 'fixed'
   elseif typename == 'ptrarray' then params.type = 'ptr'
   elseif typename == 'bytearray' then params.type = 'byte'
   end
   local variant = assert(array_map[params.type or 'array'])

   -- Add proper bytecode.
   context.ti[#context.ti + 1] = get_code(bc.ARRAY + variant, params)
   if variant == bc.VARIANT_ARRAY_FIXEDC then
      -- For fixed-length array, add length here.
      context.ti[#context.ti + 1] = assert(params.fixed)
   end

   -- Arrays always utilize guards.
   context.guard = context.guard + 1

   -- Compile guard type into the ti.
   compile_type(context, 0, params[2], direction)
   return 'pointer'
end
typehandler.ptrarray = typehandler.array
typehandler.bytearray = typehandler.array

function typehandler.list(context, typename, params, direction)
   params.ref = true
   local code = get_code(bc.LIST, params)
   if typename == 'list' then code = code + bc.VARIANT_LIST_LIST end
   context.ti[#context.ti + 1] = code

   -- Lists almost always utilize guards.
   context.guard = context.guard + 1

   -- Compile list type argument.
   compile_type(context, 0, params[2], direction)
   return 'pointer'
end
typehandler.slist = typehandler.list

function typehandler.hash(context, typename, params, direction)
   params.ref = true
   context.ti[#context.ti + 1] = get_code(bc.HASH, params)

   -- Hashes almost always utilize guards.
   context.guard = context.guard + 1

   -- Compile list type arguments.
   compile_type(context, 0, params[2], direction)
   compile_type(context, 0, params[3], direction)
   return 'pointer'
end

-- Adds compiled type info 'source' into 'target' table.
function compile_type(context, index, source, direction)
   -- Find type handler for this type.
   local typename, params
   if type(source) == 'string' then
      typename = source
      params = {}
   else
      assert(type(source) == 'table', "bad type")
      typename = source[1]
      params = source
   end
   assert(type(typename) == 'string', "bad type")

   local ns, name = typename:match('^([%w_]+)%.([%w_]+)$')
   if ns and name then
      -- Typename actually specifies repo object, so pick it up.
      local obj = core.repo[ns][name]

      -- Derive the type from the namespace object.
      typename = obj._kind
      params[2] = obj
   end

   local handler = typehandler[typename]
   assert(handler, "bad type")

   -- Invoke typehandler.
   local ffitype = handler(context, typename, params, direction, index)
   assert(ffitype, "bad type")

   -- If we have output argument, ffidef is actually 'pointer', except
   -- when it is 'return'.
   local dir = params.dir or 'in'
   if #context.def == 0 then
      -- Set up ffidef slots for return value arguments.
      context.def[1] = { ffitype, false, ffitype ~= 'void' }
   elseif index ~= 0 then
      -- Set up ffidef slots for C function argument.
      if dir == 'out' or dir == 'inout' then ffitype = 'pointer' end
      local def = context.def[index] or {}
      context.def[index] = def
      def[1] = ffitype
      def[2] = (dir == 'in' or dir == 'inout')
      def[3] = (dir == 'out' or dir == 'inout' or dir == 'out-caller-alloc')
   end
   if index ~= 0 then context.def[index].name = 'v' .. index end
end

compiler.gate = {}

function compiler.gate.c(source, target)
   -- Prepare context table holding everything needed to compile
   -- function definition.
   local context = {
      ti = {},
      def = {},
      guard = 0,
   }

   -- Process all type arguments, starting with return value.
   compile_type(context, 1, source.ret or 'void')
   for i, def in ipairs(source) do compile_type(context, i + 1, def, 'c') end

   -- Replace bool placeholders in input/output def markers with real
   -- input/output numbers.
   local argnum = { 0, 0, 0 }
   for i, def in ipairs(context.def) do
      for j = 2, 3 do
	 if def[j] then
	    argnum[j] = argnum[j] + 1
	    def[j] = argnum[j]
	 end
      end
   end

   -- Create proper call_info instance.
   local call_info = core.call.new(context.def, context.ti, context.guard)

   -- Start creating source of closure wrapper to JIT.
   local rawname = 'raw_' .. (source.name or 'anonymous'):gsub('[^_%w]', '_')
   local src = {
      'local core, call_info, target, guard_size = ...\n',
      'local call_toc = core.call.toc\n',
      'local guard_new = core.guard.new\n',
      'local function ', rawname, '(',
   }

   -- Create the list of input arguments.
   local vars = {}
   for i, def in ipairs(context.def) do
      if not def.internal and def[2] then
	 vars[#vars + 1] = def.name
      end
   end
   src[#src + 1] = table.concat(vars, ', ')
   src[#src + 1] = ')\n'

   -- Generate guard definition.
   src[#src + 1] = 'local guard'
   if context.guard > 0 then
      src[#src + 1] = ' = guard_new('
      src[#src + 1] = tostring(context.guard)
      src[#src + 1] = ')'
   end
   src[#src + 1] = '\n'

   -- Generate vars adjustments.
   for i, def in ipairs(context.def) do
      if not def[2] and def[3] then
	 src[#src + 1] = 'local '
	 src[#src + 1] = def.name
	 src[#src + 1] = '\n'
      end
   end

   -- Render the call outputs.
   vars = {}
   for i, def in ipairs(context.def) do
      if def[3] then vars[#vars + 1] = def.name end
   end
   src[#src + 1] = table.concat(vars, ', ')
   if #vars ~= 0 then src[#src + 1] = ' = ' end
   src[#src + 1] = 'call_toc(call_info, target, guard, '

   -- Render the call inputs.
   vars = {}
   for i, def in ipairs(context.def) do
      if def[2] then vars[#vars + 1] = def.name end
   end
   src[#src + 1] = table.concat(vars, ', ')
   src[#src + 1] = ')\n'

   -- Render the return values.
   vars = {}
   for i, def in ipairs(context.def) do
      if def[3] then vars[#vars + 1] = def.name end
   end
   if #vars ~= 0 then
      src[#src + 1] = 'return '
      src[#src + 1] = table.concat(vars, ' ,')
      src[#src + 1] = '\n'
   end

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
