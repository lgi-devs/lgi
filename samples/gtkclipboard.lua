#! /usr/bin/env lua

-- Gtk clipboard sample, adapted from Vala clipboard sample at
-- http://live.gnome.org/Vala/GTKSample#Clipboard

local lgi = require 'lgi'
local Gtk = lgi.require('Gtk', '3.0')
local Gdk = lgi.Gdk

local app = Gtk.Application { application_id = 'org.lgi.samples.gtkclipboard' }

function app:on_activate()
   local entry = Gtk.Entry {}
   local window = Gtk.Window {
      application = self,
      title = 'Clipboard',
      default_width = 300, default_height = 20,
      child = entry
   }
   window:show_all()

   local display = window:get_display()
   local clipboard = Gtk.Clipboard.get_for_display(
      display, Gdk.SELECTION_CLIPBOARD)

   -- Get text from clipboard
   entry.text = clipboard:wait_for_text() or ''

   -- If the user types something, set text to clipboard
   function entry:on_changed()
      clipboard:set_text(entry.text, -1)
   end
end

app:run { arg[0], ... }
