------------------------------------------------------------------------------
--
--  lgi GLib Bytes support
--
--  Copyright (c) 2013 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs, tostring, setmetatable, error, assert
   = select, type, pairs, tostring, setmetatable, error, assert

local lgi = require 'lgi'
local GLib = lgi.GLib
local Bytes = GLib.Bytes

-- Define length querying operation.
Bytes._len = Bytes.get_size

-- Add support for querying bytes attribute
Bytes._attribute = { data = { get = Bytes.get_data } }
