--[[-- Assorted tests. --]]--

require 'lgi'
local GLib = require 'lgi.GLib'
local Gio = require 'lgi.Gio'
local Gtk = require 'lgi.Gtk'

tests = {}

tests[1] =
   function()
      local stream
      local main = GLib.MainLoop.new(nil, false)
      file = Gio.file_new_for_path('test.lua')
      file:read_async(GLib.PRIORITY_DEFAULT, nil,
		      function(o, asr)
			 print(string.format(
				  'read_closure(%s, %s)', tostring(o),
				  tostring(asr)))
			 stream = file:read_finish(asr)
			 main:quit()
		      end)
      main.run()
      assert(stream)
   end

tests[2] =
   -- Based on test from LuiGI code.  Thanks Adrian!
   function()
      Gtk.init(0, nil)
      local window = Gtk.Window {
	 title = 'window',
	 default_width = 400,
	 default_height = 300
      }
      local status_bar = Gtk.Statusbar { has_resize_grip = true }
      local toolbar = Gtk.Toolbar()
      local vbox = Gtk.VBox()
      local ctx = status_bar:get_context_id('default')
      status_bar:push(ctx, 'This is statusbar message.')
      toolbar:insert(Gtk.ToolButton { 
			stock_id = 'gtk-quit',
			on_clicked = Gtk.main_quit
		     }, -1)
      toolbar:insert(Gtk.ToolButton { 
			stock_id = 'gtk-about',
			on_clicked = function()
					local dlg = Gtk.AboutDialog {
					   program_name = 'LGI Demo',
					   title = 'About...',
					   license = 'MIT'
					}
					dlg:run()
					dlg:hide()
				     end
		     }, -1)
      vbox:pack_start(toolbar, false, false, 0)
      vbox:pack_start(Gtk.Label { label = 'Contents' }, true, true, 0)
      vbox:pack_end(status_bar, false, false, 0)
      window:add(vbox)
      window:show_all()
      Gtk.main()
   end

tests[2]()

for num, fun in ipairs(tests) do
   local ok, msg = pcall(fun)
   if ok then
      print(string.format('PASS: %d', num))
   else
      print(string.format('FAIL: %d: %s', num, tostring(msg)))
   end
end
