------------------------------------------------------------------------------
--
--  LGI Lua-side core.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local assert, setmetatable, getmetatable, type, pairs, string, rawget,
table, require, tostring, error, pcall, ipairs, unpack,
next, select =
   assert, setmetatable, getmetatable, type, pairs, string, rawget,
   table, require, tostring, error, pcall, ipairs, unpack or table.unpack,
   next, select
local package, math = package, math

-- Require core lgi utilities, used during bootstrap.
local core = require 'lgi.core'

-- Initialize GI wrapper from the core.
local gi = core.gi
assert(gi.require ('GLib', '2.0'))
assert(gi.require ('GObject', '2.0'))

-- Create lgi table, containing the module.
local lgi = { _NAME = 'lgi', _VERSION = require 'lgi.version' }

-- Add simple flag-checking function, avoid compatibility hassle with
-- importing bitlib just because of this simple operation.
function core.has_bit(value, flag)
   return value % (2 * flag) >= flag
end

-- Forward 'yield' functionality into external interface.
lgi.yield = core.yield

-- If global package 'bytes' does not exist (i.e. not provided
-- externally), use our internal (although incomplete) implementation.
local ok, bytes = pcall(require, 'bytes')
if not ok or not bytes then
   package.loaded.bytes = core.bytes
end

-- Prepare logging support.  'log' is module-exported table, containing all
-- functionality related to logging wrapped around GLib g_log facility.
lgi.log = require 'lgi.log'

-- For the rest of bootstrap, prepare logging to lgi domain.
local log = lgi.log.domain('lgi')
log.message('gobject-introspection binding for Lua, ' .. lgi._VERSION)

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.  Created by C-side bootstrap.
local repo = core.repo

local component = require 'lgi.component'
local record = require 'lgi.record'
local class = require 'lgi.class'

-- Table containing loaders for various GI types, indexed by
-- gi.InfoType constants.
local typeloader = {}

typeloader['function'] =
   function(namespace, info)
      return core.callable.new(info), '_function'
   end

function typeloader.constant(namespace, info)
   return core.constant(info), '_constant'
end

local function load_enum(info, meta)
   local value = {}

   -- Load all enum values.
   local values = info.values
   for i = 1, #values do
      local mi = values[i]
      value[mi.name:upper()] = mi.value
   end

   -- Install metatable providing reverse lookup (i.e name(s) by
   -- value).
   setmetatable(value, meta)
   return value
end

-- Enum reverse mapping, value->name.
local enum_mt = {}
function enum_mt:__index(value)
   for name, val in pairs(self) do
      if val == value then return name end
   end
end

function typeloader.enum(namespace, info)
   return load_enum(info, enum_mt), '_enum'
end

-- Resolving arbitrary number to the table containing symbolic names
-- of contained bits.
local bitflags_mt = {}
function bitflags_mt:__index(value)
   if type(value) ~= 'number' then return end
   local t = {}
   for name, flag in pairs(self) do
      if type(flag) == 'number' and core.has_bit(value, flag) then
	 t[name] = flag
      end
   end
   return t
end

function typeloader.flags(namespace, info)
   return load_enum(info, bitflags_mt), '_enum'
end

function typeloader.struct(namespace, info)
   -- Avoid exposing internal structs created for object implementations.
   if not info.is_gtype_struct then
      return record.load(info), '_struct'
   end
end

function typeloader.union(namespace, info)
   return record.load(info), '_union'
end

function typeloader.interface(namespace, info)
   return class.load_interface(namespace, info), '_interface'
end

function typeloader.object(namespace, info)
   return class.load_class(namespace, info), '_class'
end

-- Repo namespace metatable.
local namespace_mt = {
   _categories = { '_class', '_interface', '_struct', '_union', '_enum',
		   '_function', '_constant', } }

-- Gets symbol of the specified namespace, if not present yet, tries to load it
-- on-demand.
function namespace_mt:__index(symbol)
   -- Check whether symbol is present in the metatable.
   local val = namespace_mt[symbol]
   if val then return val end

   -- Check, whether there is some precondition in the lazy-loading table.
   local preconditions = rawget(self, '_precondition')
   local precondition = preconditions and preconditions[symbol]
   if precondition then
      local package = preconditions[symbol]
      if not preconditions[package] then
	 preconditions[package] = true
	 require('lgi.override.' .. package)
	 preconditions[package] = nil
      end
      preconditions[symbol] = nil
      if not next(preconditions) then self._precondition = nil end
   end

   -- Check, whether symbol is already loaded.
   val = component.mt._element(self, nil, symbol, namespace_mt._categories)
   if val then return val end

   -- Lookup baseinfo of requested symbol in the GIRepository.
   local info = gi[self._name][symbol]
   if not info then return nil end

   -- Decide according to symbol type what to do.
   local loader = typeloader[info.type]
   if loader then
      local category
      val, category = loader(self, info)

      -- Cache the symbol in specified category in the namespace.
      if val then
	 local cat = rawget(self, category)
	 if not cat then
	    cat = {}
	    self[category] = cat
	 end
	 -- Store symbol into the repo, but only if it is not already
	 -- there.  It could by added to repo as byproduct of loading
	 -- other symbol.
	 if not cat[symbol] then cat[symbol] = val end
      end
   end
   return val
end

-- Resolves everything in the namespace by iterating through it.
function namespace_mt:_resolve(recurse)
   -- Iterate through all items in the namespace and dereference them,
   -- which causes them to be loaded in and cached inside the namespace
   -- table.
   local gi_ns = gi[self._name]
   for i = 1, #gi_ns do
      local ok, component = pcall(function() return self[gi_ns[i].name] end)
      if ok and recurse and type(component) == 'table' then
	 local resolve = component._resolve
	 if resolve then resolve(component, recurse) end
      end
   end
   return self
end

-- Makes sure that the namespace (optionally with requested version)
-- is properly loaded.
function lgi.require(name, version)
   -- Load the namespace info for GIRepository.  This also verifies
   -- whether requested version can be loaded.
   local ns_info = assert(gi.require(name, version))

   -- If the repository table does not exist yet, create it.
   local ns = rawget(repo, name)
   if not ns then
      ns = setmetatable({ _name = name, _version = ns_info.version,
			  _dependencies = ns_info.dependencies },
			namespace_mt)
      repo[name] = ns

      -- Make sure that all dependent namespaces are also loaded.
      for name, version in pairs(ns._dependencies or {}) do
	 lgi.require(name, version)
      end

      -- Try to load override, if it is present.
      local override_name = 'lgi.override.' .. ns._name
      local ok, msg = pcall(require, override_name)
      if not ok then
	 -- Try parsing message; if it is something different than
	 -- "module xxx not found", then attempt to load again and let
	 -- the exception fly out.
	 if not msg:find("module '" .. override_name .. "' not found:",
			 1, true) then
	    package.loaded[override_name] = nil
	    require(override_name)
	 end
      end
   end
   return ns
end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(_, name) return lgi.require(name) end })

repo.GObject._precondition = {}
for _, name in pairs { 'Type', 'Value', 'Closure', 'Object' } do
   repo.GObject._precondition[name] = 'GObject-' .. name
end
repo.GObject._precondition.InitiallyUnowned = 'GObject-Object'

-- Create lazy-loading components for variant stuff.
repo.GLib._precondition = {}
for _, name in pairs { 'Variant', 'VariantType', 'VariantBuilder' } do
   repo.GLib._precondition[name] = 'GLib-Variant'
end

-- Access to module proxies the whole repo, for convenience.
return setmetatable(lgi, { __index = repo })
