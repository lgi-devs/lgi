--[[--------------------------------------------------------------------------

  LGI testsuite, progress callback checking

  Copyright (c) 2022 Nicola Fontana
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local lgi = require 'lgi'
local core = require 'lgi.core'
local GObject = lgi.GObject
local Gio = lgi.Gio
local GLib = lgi.GLib

local check = testsuite.check

local progress = testsuite.group.new('progress')

local function check_gerror(namespace, api, ...)
    local result, err = namespace[api](...)
    check(result, string.format('Error during %s() call: %s',
				api, tostring(err)))
    return result
end

function progress.file_copy()
    local File = Gio.File
    local loop = GLib.MainLoop.new()


    -- This assumes a valid and readable filename is passed as arg[0]
    local src = check_gerror(File, 'new_for_path', arg[0])
    local dst = check_gerror(File, 'new_tmp')
    local flags = Gio.FileCopyFlags.OVERWRITE
    local priority = 0
    local cancellable = nil

    local progress_callback = function (partial, total)
	check(partial <= total,
	      string.format('Writing too many bytes (%d > %d)', partial, total))
    end

    local finish_callback = function (self, result)
	check_gerror(Gio.File, 'copy_finish', self, result)
	loop:quit()
    end
    if core.repo.GLib.check_version(2, 82, 0) then
        src:copy_async(dst, flags, priority, cancellable,
            progress_callback, finish_callback)
    else
        src:copy_async(dst, flags, priority, cancellable,
            GObject.Closure(progress_callback), GObject.Closure(finish_callback))
    end
    loop:run()
end
