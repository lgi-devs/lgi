--[[--------------------------------------------------------------------------

  LGI testsuite, GIo test suite.

  Copyright (c) 2016 Uli Schlachter
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local core = require 'lgi.core'

local check = testsuite.check
local checkv = testsuite.checkv

local gio = testsuite.group.new('gio')

function gio.read()
    local GLib, Gio = lgi.GLib, lgi.Gio

    -- Prepare the input to read
    local input
    input = "line"
    input = Gio.MemoryInputStream.new_from_data(input)
    input = Gio.DataInputStream.new(input)

    local line, length

    -- Read line
    line, length = input:read_line()
    checkv(line, "line", "string")
    checkv(length, 4, "number")

    -- Read EOF
    line, length = input:read_line()
    checkv(line, nil, "nil")
    checkv(length, 0, "number")
end
