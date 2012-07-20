------------------------------------------------------------------------------
--
--  LGI Lua-side core.
--
--  Copyright (c) 2010-2012 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local require, setmetatable = require, setmetatable
local package = require 'package'

-- Require core lgi utilities, used during bootstrap.
local core = require 'lgi.core'

-- Create lgi table, containing the module.
local lgi = { _NAME = 'lgi', _VERSION = require 'lgi.version' }

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

-- Repository, table with all loaded namespaces.  Its metatable takes care of
-- loading on-demand.
local repo = {}
lgi.require = function(name, version) end

-- Install metatable into repo table, so that on-demand loading works.
setmetatable(repo, { __index = function(_, name)
				  return lgi.require(name)
			       end })

-- Access to module proxies the whole repo, so that lgi.'namespace'
-- notation works.
return setmetatable(lgi, { __index = repo })
