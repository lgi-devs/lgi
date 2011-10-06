------------------------------------------------------------------------------
--
--  LGI Gio2 override module.
--
--  Copyright (c) 2010, 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs = select, type, pairs
local lgi = require 'lgi'
local core = require 'lgi._core'
local Gio = lgi.Gio
local GObject = lgi.GObject
