------------------------------------------------------------------------------
--
--  LGI Gst override module.
--
--  Copyright (c) 2010 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local lgi = require 'lgi'
local gi = require('lgi._core').gi
local Gst = lgi.Gst

-- GstObject has special ref_sink mechanism, make sure that lgi core
-- is aware of it, otherwise refcounting is screwed.
Gst.Object._sink = gi.Gst.Object.methods.ref_sink

-- Load additional Gst modules.
local GstInterfaces = lgi.GstInterfaces
