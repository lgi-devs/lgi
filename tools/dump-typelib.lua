#! /usr/bin/env lua
------------------------------------------------------------------------------
--
--  LGI tools for dumping typelib fragments into readable text format.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local lgi_core = require 'lgi.core'
local gi = lgi_core.gi
require 'debugger'

-- Implements BaseInfo object, capable of dump itself.
local infos = {}
infos.base = { attrs = { 'name', 'namespace', 'type', 'deprecated' },
	       cats = {}, }
infos.base.__index = infos.base

-- Creates new info wrapper according to info type.
function infos.new(info)
   if info then
      return setmetatable({ info = info }, infos[info.type] or infos.base)
   end
end

-- Derives new baseinfo subtype.
function infos.base:derive(attrs, cats)
   local new_attrs = {}
   for _, val in ipairs(self.attrs) do new_attrs[#new_attrs + 1] = val end
   for _, val in ipairs(attrs or {}) do new_attrs[#new_attrs + 1] = val end
   local new_cats = {}
   for _, val in ipairs(self.cats) do new_cats[#new_cats + 1] = val end
   for _, val in ipairs(cats or {}) do new_cats[#new_cats + 1] = val end
   local new = setmetatable({ attrs = new_attrs, cats = new_cats }, self)
   new.__index = new
   return new
end

-- Gets given attribute or category.
function infos.base:get(name, depth)
   local item = self.info[name]
   if gi.isinfo(item) then
      item = infos.new(item)
      if depth then item = item:dump(depth) end
   else
      for _, cat in pairs(self.cats) do
	 if cat == name then item = infos.category.new(item) end
      end
   end
   return item
end

-- Dumps all attributes into the target table.
function infos.base:dump_attrs(target, depth)
   for _, attr in ipairs(self.attrs) do
      target[attr] = self:get(attr, depth - 1)
   end
   return attrs
end

-- Dumps all categories into the target table.
function infos.base:dump_cats(target, depth)
   local cats = {}
   for _, cat in ipairs(self.cats) do
      target[cat] = self:get(cat):dump(depth - 1)
   end
   return cats
end

function infos.base:dump(depth)
   if depth <= 0 then return '...' end
   local t = {}
   self:dump_attrs(t, depth)
   local cats = {}
   self:dump_cats(cats, depth)
   if next(cats) then t.cats = cats end
   return t
end

-- Implementation of 'subcategory' pseudoinfo.
infos.category = infos.base:derive()
function infos.category.new(category)
   return setmetatable({ info = category }, infos.category)
end

function infos.category:dump(depth)
   local t = {}
   for i = 1, #self.info do
      t[i] = infos.new(self.info[i]):dump(depth)
   end
   return t
end

infos.type = infos.base:derive(
   { 'tag', 'is_basic', 'interface', 'array_type',
     'is_zero_terminated', 'array_length', 'fixed_size', 'is_pointer' },
   { 'params' })

function infos.type:dump_cats(target, depth)
   local params = {}
   for i, param in ipairs(self.info.params or {}) do
      params[i] = infos.new(param):dump(depth - 1)
   end
   if next(params) then target.params = params end
end

infos.registered = infos.base:derive({ 'gtype' }, {})
infos.object = infos.registered:derive(
   { 'parent', 'type_struct', },
   { 'interfaces', 'fields', 'vfuncs', 'methods', 'constants', 'properties',
     'signals' })
infos.interface = infos.registered:derive(
   { 'type_struct', },
   { 'prerequisites', 'vfuncs', 'methods', 'constants', 'properties',
     'signals' })
infos.property = infos.base:derive({ 'typeinfo', 'flags', 'transfer' })
infos.callable = infos.base:derive(
   { 'return_type', 'return_transfer' },
   { 'args' })
infos['function'] = infos.callable:derive({ 'flags' })
infos.signal = infos.callable:derive({ 'flags' })
infos.callback = infos.callable:derive()
infos.vfunc = infos.callable:derive()
infos.arg = infos.base:derive(
   { 'typeinfo', 'direction', 'transfer', 'optional', 'typeinfo' }
)
infos.struct = infos.registered:derive({ 'is_gtype_struct', 'size' },
				       { 'fields', 'methods' })
infos.union = infos.registered:derive({ 'size' }, { 'fields', 'methods' })
infos.field = infos.base:derive({ 'typeinfo', 'flags', 'size', 'offset' })
infos.enum = infos.registered:derive({ 'storage' }, { 'values' })
infos.value = infos.base:derive({ 'value' })
infos.constant = infos.base:derive({ 'typeinfo', 'value' })

-- Implementation of info wrapper for namespace pseudoinfo.
infos.namespace = infos.base:derive({ 'name', 'version', 'dependencies' })
function infos.namespace:get(name)
   local item = self.info[name]
   return item and infos.new(item)
end

function infos.namespace:dump_cats(target, depth)
   if depth <= 0 then return '...' end
   for i = 1, #self.info do
      local info = self.info[i]
      target[info.name] = infos.new(info):dump(depth - 1)
   end
end

function infos.namespace.new(info)
   return setmetatable({ info = info }, infos.namespace)
end

-- Implementation of root element pseudoinfo.
infos.root = infos.base:derive()
function infos.root:get(name)
   return infos.namespace.new(gi.require(name))
end

-- Commandline processing
arg = arg or {}
paths = {}
depth = 3
for i = 1, #arg do
   if tonumber(arg[i]) then depth = tonumber(arg[i])
   else paths[#paths + 1] = arg[i] end
end

-- Go through all paths and dump them.
for _, path in ipairs(paths) do
   local info = infos.root
   for name in path:gmatch('([^%.]+)%.?') do
      info = info:get(name)
      if not info then break end
   end
   if not info then error(('%s not found'):format(path)) end
   dump(info:dump(depth), depth * 2)
end
