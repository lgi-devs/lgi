------------------------------------------------------------------------------
--
--  lgi Gio override module.
--
--  Copyright (c) 2010, 2011, 2013, 2016 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs, setmetatable, rawset, unpack =
   select, type, pairs, setmetatable, rawset, unpack or table.unpack
local coroutine = require 'coroutine'

local lgi = require 'lgi'
local GLib = lgi.GLib
local GObject = lgi.GObject
local Gio = lgi.Gio

local component = require 'lgi.component'
local class = require 'lgi.class'
local namespace = require 'lgi.namespace'
local core = require 'lgi.core'
local gi = core.gi

-- Create completely new 'Async' pseudoclass, wrapping lgi-specific
-- async helpers.
local async_context = setmetatable({}, { __mode = 'k' })
Gio.Async = setmetatable(
   {}, {
      __index = function(self, key)
	 if key == 'io_priority' or key == 'cancellable' then
	    return (async_context[coroutine.running()] or {})[key]
	 end
      end,

      __newindex = function(self, key, value)
	 if key == 'io_priority' or key == 'cancellable' then
	    (async_context[coroutine.running()])[key] = value
	 else
	    rawset(self, key, value)
	 end
      end,
})

-- Creates new context for new coroutine and stores it into async_context.
local function register_async(coro, cancellable, io_priority)
   local current = async_context[coroutine.running()] or {}
   async_context[coro] = {
      io_priority = io_priority or current.io_priority or GLib.PRIORITY_DEFAULT,
      cancellable = cancellable or current.cancellable,
   }
end

function Gio.Async.start(func, cancellable, io_priority)
   -- Create coroutine and store context for it.
   local coro = coroutine.create(func)
   register_async(coro, cancellable, io_priority)

   -- Return coroutine starter.
   return function(...)
      return coroutine.resume(coro, ...)
   end
end

function Gio.Async.call(func, cancellable, io_priority)
   -- Create mainloop which will create modality.
   local loop = GLib.MainLoop()
   local results

   -- Create coroutine around function wrapper which will invoke
   -- target and then terminate the loop.
   local coro = coroutine.create(function(...)
	 (function(...)
	     results = { n = select('#', ...), ... }
	     loop:quit()
	 end)(func(...))
   end)

   -- Register coroutine.
   register_async(coro, cancellable, io_priority)

   -- Return starter closure.
   return function(...)
      -- Spawn it inside idle handler, to avoid hang in case that
      -- coroutine finishes during its first resuming.
      local args = { n = select('#', ...), ... }
      GLib.idle_add(
	 GLib.PRIORITY_DEFAULT, function()
	    coroutine.resume(coro, unpack(args, 1, args.n))
      end)

      -- Spin the loop.
      loop:run()

      -- Unpack results.
      return unpack(results, 1, results.n)
   end
end

-- Add 'async_' method handling.  Dynamically generates wrapper around
-- xxx_async()/xxx_finish() sequence using currently running
-- coroutine.
local tag_int = gi.GObject.ParamSpecEnum.fields.default_value.typeinfo.tag
local function async_element(name, accessor, param_self, param_object)
   -- Check, whether we have async_xxx request.
   local name_root = type(name) == 'string' and name:match('^async_(.+)$')
   if name_root then
      local async = accessor(param_self, param_object, name_root .. '_async')
      local finish = accessor(param_self, param_object, name_root .. '_finish')

      -- Some clients name async method just 'name' and use
      -- 'name_sync' for synchronous variant.
      if finish and not async and accessor(param_self, param_object,
					   name_root .. '_sync') then
	 async = accessor(param_self, param_object, name_root)
      end

      -- We have async/finish pair, create element table containing
      -- information how to synthesize calling function.
      if async and finish then
	 element = { name = name_root, in_args = 0,
		     async = async, finish = finish, }

	 -- Go through arguments of async method and find indices of
	 -- io_priority, cancellable and callback args.
	 for _, param in ipairs(async.params) do
	    if param['in'] then
	       element.in_args = element.in_args + 1
	       if not param['out'] and param.typeinfo then
		  if param.name == 'io_priority' and
		  param.typeinfo.tag == tag_int then
		     element.io_priority = element.in_args
		  elseif param.name == 'cancellable' and
		  param.typeinfo.tag == 'interface' then
		     element.cancellable = element.in_args
		  end
	       end
	    end
	 end

	 -- Returns accumulated async call data.
	 return element, '_async'
      end
   end
end

local function async_access(element, process_yield)
   -- Generate wrapper method calling _async/_finish pair automatically.
   return function(...)
      -- Check that we are running inside context.
      local context = async_context[coroutine.running()]
      if not context then
	 error(("%s.async_%s: called out of async context"):format(
		  self._name, element.name), 4)
      end

      -- Create input args, intersperse them with automatic ones.
      local args, i, index = {}, 1, 1
      for i = 1, element.in_args - 1 do
	 if i == element.io_priority then
	    args[i] = context.io_priority
	 elseif i == element.cancellable then
	    args[i] = context.cancellable
	 else
	    args[i] = select(index, ...)
	    index = index + 1
	 end
      end
      args[element.in_args] = coroutine.running()

      element.async(unpack(args, 1, element.in_args))
      return element.finish(process_yield(coroutine.yield()))
   end
end

local inherited_class_element = class.class_mt._element
function class.class_mt:_element(object, name)
   local element, category = inherited_class_element(self, object, name)
   if element then return element, category end
   return async_element(name, inherited_class_element, self, object)
end

function class.class_mt:_index_async(element)
   return async_access(element, function(_, ...) return ... end)
end

local inherited_gobject_element = GObject.Object._element
function GObject.Object:_element(object, name)
   local element, category = inherited_gobject_element(self, object, name)
   if element then return element, category end
   return async_element(name, inherited_gobject_element, self, object)
end

function GObject.Object:_access_async(object, element, ...)
   if select('#', ...) > 0 then
      error(("%s: `%s' not writable"):format(
	       core.object.query(object, 'repo')._name, name))
   end

   return async_access(element, function(...) return ... end)
end

-- Enforce that Gio._function category is already created
local _ = Gio.bus_get
function namespace.mt._category_mt._function:__index(key)
   local element = async_element(key, function(_, _, name)
				    return self._namespace[name]
   end)
   return element and async_access(element, function(_, ...) return ... end)
end

function Gio.Initable._init2(object)
   -- Avoid passing cancellable, because it might cause init() to
   -- fail, even if we could retrieve cancellable from async context.
   return object:init()
end

function Gio.AsyncInitable._init1(object)
   -- Check, whether we are running in async context.  If not, skip
   -- initializer (do not fail it).
   if not async_context[coroutine.running()] then return '_initskip' end

   -- Invoke async initializer.
   return object:async_init()
end

-- GOI < 1.30 did not map static factory method into interface
-- namespace.  The prominent example of this fault was that
-- Gio.File.new_for_path() had to be accessed as
-- Gio.file_new_for_path(). Create a compatibility layer to mask this
-- flaw.
for _, name in pairs { 'path', 'uri', 'commandline_arg' } do
   if not Gio.File['new_for_' .. name] then
      Gio.File['new_for_' .. name] = Gio['file_new_for_' .. name]
   end
end

-- Older versions of gio did not annotate input stream methods as
-- taking an array.  Apply workaround.
-- https://github.com/pavouk/lgi/issues/59
for _, name in pairs { 'read', 'read_all', 'read_async' } do
   local raw_read = Gio.InputStream[name]
   if gi.Gio.InputStream.methods[name].args[1].typeinfo.tag ~= 'array' then
      Gio.InputStream[name] = function(self, buffer, ...)
	 return raw_read(self, buffer, #buffer, ...)
      end
      if name == 'read_async' then
	 raw_finish = Gio.InputStream.read_finish
	 function Gio.InputStream.async_read(stream, buffer)
	    raw_read(stream, buffer, Gio.Async.io_priority,
		     Gio.Async.cancellable, coroutine.running())
	    return raw_finish(coroutine.yield())
	 end
      end
   end
end

-- Add preconditions for auto-loading DBus overrides.
Gio._precondition = {}
for _, name in pairs {
   'AnnotationInfo', 'ArgInfo', 'MethodInfo', 'SignalInfo', 'PropertyInfo',
   'InterfaceInfo', 'NodeInfo',
} do
   Gio._precondition['DBus' .. name] = 'Gio-DBus'
end
