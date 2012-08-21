#! /usr/bin/env lua

--[[
This example is the rewrite of Michael Schurter's sample in Lua.
Copyleft (C) 2012 Ildar Mulyukov
Original python program is [here](http://blog.schmichael.com/2008/10/30/listing-all-passwords-stored-in-gnome-keyring/)
]]--

local lgi = require 'lgi'
local GnomeKeyring = lgi.require 'GnomeKeyring'

-- main(argv):

local _, keyring_names = GnomeKeyring.list_keyring_names_sync()
for _, keyring in ipairs(keyring_names) do
   local _, item_ids = GnomeKeyring.list_item_ids_sync(keyring)
   for _, id in ipairs(item_ids) do
      local err, item = GnomeKeyring.item_get_info_sync(keyring, id)
      if err == 'IO_ERROR' then
         print ('[' .. keyring .. '] --locked--')
         break
      end
      print ('[' .. keyring .. '] ' .. item:get_display_name() .. ' = ' .. 
         string.gsub(item:get_secret(), ".", "*") )
   end
   if #item_ids == 0 then
      print ('[' .. keyring .. '] --empty--')
   end
end
