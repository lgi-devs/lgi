#! /usr/bin/env lua

--
-- Sample server using libsoup library.  Listens on 1080 port and serves
-- local files from current directory.  Allows to be terminated by query
-- for /quit file (i.e. curl http://localhost:1080/quit)
--

local coroutine = require 'coroutine'

local lgi = require 'lgi'
local bytes = require 'bytes'
local GLib = lgi.GLib
local Gio = lgi.Gio
local Soup = lgi.Soup

local app = Gio.Application { application_id = 'org.lgi.soupsvr' }
function app:on_activate()
   app:hold()

   local server = Soup.Server { port = 1080 }

   -- Set up quit handler.
   server:add_handler('/quit', function(server, msg, path, query, ctx)
      msg.status_code = 200
      msg.response_body:complete()
      server:quit()
      app:release()
   end)

   -- Set up file retriever handler.
   server:add_handler('/', function(server, msg, path, query, ctx)
      local stream = Gio.File.new_for_path(path:sub(2)):read()
      if stream then
	 local next_chunk = coroutine.wrap(function()
	    local buffer = bytes.new(4096)
	    while true do
	       stream:read_async(buffer, #buffer, GLib.PRIORITY_DEFAULT, nil,
				 coroutine.running())
	       local size = stream.read_finish(coroutine.yield())
	       if size < 0 then
		  server:quit()
		  app:release()
		  return
	       end
	       msg.response_body:append(tostring(buffer):sub(1, size))
	       server:unpause_message(msg)
	       coroutine.yield()
	       if (size < #buffer) then break end
	    end
	    msg.response_body:complete()
	 end)

	 msg.status_code = 200
	 msg.response_headers:set_encoding('CHUNKED')
	 msg.on_wrote_headers = next_chunk
	 msg.on_wrote_chunk = next_chunk

      else
	 msg.status_code = 404
	 msg.response_body:complete()
      end
   end)

   -- Start the server running asynchronously.
   server:run_async()
end

app:run { arg[0], ... }
