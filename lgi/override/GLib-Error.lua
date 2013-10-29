-------------------------------------------------------------------------------
--
-- lgi GLib Error support implementation.
--
-- Copyright (c) 2013 Pavel Holejsovsky
-- Licensed under the MIT license:
-- http://www.opensource.org/licenses/mit-license.php
--
-------------------------------------------------------------------------------

local pairs, type, select
   = pairs, type, select

local lgi = require 'lgi'
local core = require 'lgi.core'
local record = require 'lgi.record'

local GLib = lgi.GLib

local Error = lgi.GLib.Error
Error._attribute = {}

-- Add attribute which looks up associated enum type.
Error._attribute.domain = {
   get = function(object)
      local info = core.gi[core.record.field(object, Error._field.domain)]
      return core.repotype(info)
   end,
}

-- Override code attribute to return symbolic code from enum.
Error._attribute.code = {
   get = function(object)
      local code = core.record.field(object, Error._field.code)
      local enum = object.domain
      return enum and enum[code] or code
   end,
}

-- Converts domain to quark
local function domain_to_quark(domain)
   if type(domain) == 'table' then
      return domain.error_domain
   elseif type(domain) == 'string' then
      return GLib.quark_from_string(domain)
   else
      return domain
   end
end

-- Converts code to numeric code, assumes already quark-form domain
local function code_to_number(code, domain)
   if type(code) == 'string' then
      return core.repotype(core.gi[domain])(code)
   else
      return code
   end
end

-- Create new error instance.
function Error.new(domain, code, message, ...)
   domain = domain_to_quark(domain)
   code = code_to_number(code, domain)
   if select('#', ...) > 0 then
      message = message:format(...)
   end
   return Error.new_literal(domain, code, message)
end

function Error:_new(...)
   return Error.new(...)
end

-- Override _tostring, in order to automatically convert errors to
-- error messages.
function Error:_tostring()
   return self.message
end

-- Override matches() method, so that it can use different methods of
-- entering domain and/or code.
function Error:matches(domain, code)
   if GLib.Error:is_type_of(domain) then
      -- This actually means match against another error instance.
      local err = domain
      domain = core.record.field(err, GLib.Error._field.domain)
      code = core.record.field(err, GLib.Error._field.code)      
   else
      domain = domain_to_quark(domain)
      code = code_to_number(code, domain)
   end
   return Error._method.matches(self, domain, code)
end
