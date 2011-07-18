------------------------------------------------------------------------------
--
--  LGI Clutter override module.
--
--  Copyright (c) 2010 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs = select, type, pairs
local lgi = require 'lgi'
local core = require 'lgi._core'
local Clutter = lgi.Clutter
local GObject = lgi.GObject

-- Automatically initialize clutter.
Clutter.init()
