-- Adapted from this Vala example: https://wiki.gnome.org/Projects/Vala/GIOCompressionSample
-- Can be easily made async if requied
-- Call without arguments for usage

-- Compress file to fname.gz and decompress it into fname_out
-- fname_out must be identical to original file
-- fname.gz can be decompressed by various utilites, including `zcat`

local lgi = require("lgi")
local assert = lgi.assert
local GLib, Gtk, Gdk, Gio, GObject = lgi.GLib, lgi.Gtk, lgi.Gdk, lgi.Gio, lgi.GObject

-- Take two streams and converter
local function convert(src, dst, conv)
	local convstream = Gio.ConverterOutputStream.new(dst, conv)
	convstream:splice(src, 'NONE')
	convstream:close() -- vital to flush stream
end

-- Take two files and converter
local function convert_file(src, dst, conv)
	local src_stream = assert(src:read())
	local dst_stream = assert(dst:replace(nil, false, 'NONE'))
	convert(src_stream, dst_stream, conv)
	src_stream:close()
	dst_stream:close()
end

local function fsize(file)
	return file:query_info('standard::size', 'NONE'):get_size()
end

-- Refer to https://developer.gnome.org/gio/stable/GZlibCompressor.html#GZlibCompressorFormat
-- for possible values
local format = 'GZIP'

if #arg ~= 1 and #arg ~= 2 then
	error(table.concat{'Usage: ', arg[0], 'testfile [compression (0-9)]'})
end

local compression = arg[2] and tonumber(arg[2]) or -1
assert(compression >= -1 and compression <= 9, 'Wrong compression level')

print('Starting')

local infile = Gio.File.new_for_commandline_arg(arg[1])

if not infile:query_exists() then
	error('Given file does not exist!')
end

local zipfile = Gio.File.new_for_commandline_arg(arg[1]..'.gz')
local outfile = Gio.File.new_for_commandline_arg(arg[1]..'_out')

convert_file(infile, zipfile, Gio.ZlibCompressor.new(format, compression))
convert_file(zipfile, outfile, Gio.ZlibDecompressor.new(format))

assert(fsize(infile) == fsize(outfile), 'Original/decompressed size mismatch')

print('Sizes:')
print('Original   ', fsize(infile))
print('Compressed ', fsize(zipfile))
print('Output     ', fsize(outfile))
