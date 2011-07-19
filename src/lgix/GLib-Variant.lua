------------------------------------------------------------------------------
--
--  LGI GLib Variant support implementation.
--
--  Copyright (c) 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs = select, type, pairs
local lgi = require 'lgi'
local core = require 'lgi._core'
local gi = core.gi
local GLib = lgi.GLib

-- Remove lazy loader stubs.
GLib.Variant = nil
GLib.VariantType = nil
GLib.VariantBuilder = nil

local Variant = GLib.Variant
local variant_info = gi.GLib.Variant

-- Add custom refsink and free methods for variant handling.
Variant._refsink = variant_info.methods.ref_sink
Variant._free = variant_info.methods.unref

-- VariantBuilder is boxed only in glib 2.29, older libs need custom
-- recipe how to free it.
local VariantBuilder = GLib.VariantBuilder
VariantBuilder._free = gi.GLib.VariantBuilder.methods.unref
VariantBuilder._constructor = core.callable.new(
   gi.GLib.VariantBuilder.methods.new)

-- Map VariantType.new to implicit constructor
local VariantType = GLib.VariantType
VariantType._constructor = core.callable.new(gi.GLib.VariantType.methods.new)
