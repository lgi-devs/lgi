return function(parent, dir)

local lgi = require 'lgi'
local Gtk = lgi.Gtk

local window = Gtk.Window {
   title = 'Links',
   border_width = 12,
   Gtk.Label {
      id = 'label',
      label = [[Some <a href="http://en.wikipedia.org/wiki/Text"title="plain text">text</a> may be marked up
as hyperlinks, which can be clicked
or activated via <a href="keynav">keynav</a>]],
      use_markup = true,
   },
}

function window.child.label:on_activate_link(uri)
   if uri == 'keynav' then
      local dialog = Gtk.MessageDialog {
	 transient_for = self:get_toplevel(),
	 destroy_with_parent = true,
	 message_type = 'INFO',
	 buttons = 'OK',
	 use_markup = true,
	 text = [[The term <i>keynav</i> is a shorthand for ]]
	    .. [[keyboard navigation and refers to the process of using ]]
	    .. [[a program (exclusively) via keyboard input.]],
	 on_response = Gtk.Widget.destroy,
      }
      dialog:present()
      return true
   else
      return false
   end
end

window:show_all()
return window
end,

"Links",

table.concat {
   [[Gtk.Label can show hyperlinks. The default action is to call ]],
   [[Gtk.show_uri() on their URI, but it is possible to override ]],
   [[this with a custom handler.]],
}
