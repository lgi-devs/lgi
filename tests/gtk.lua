--[[--------------------------------------------------------------------------

  LGI testsuite, Gtk overrides test group.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local io = require 'io'
local os = require 'os'
local lgi = require 'lgi'
local GObject = lgi.GObject
local Gtk = lgi.Gtk

local check = testsuite.check
local checkv = testsuite.checkv
local gtk = testsuite.group.new('gtk')

function gtk.widget_style()
   local w = Gtk.ProgressBar()
   local v = GObject.Value(GObject.Type.INT)
   w:style_get_property('xspacing', v)
   checkv(w.style.xspacing, v.value, 'number')
   check(not pcall(function() return w.style.nonexistent end))
end

function gtk.buildable_id()
   local w = Gtk.Label()
   checkv(w.id, nil, nil)
   w.id = 'label_id'
   checkv(w.id, 'label_id', 'string')
   checkv(Gtk.Buildable.get_name(w), 'label_id', 'string')
   Gtk.Buildable.set_name(w, 'new_id')
   checkv(w.id, 'new_id', 'string')
end

function gtk.container_property()
   local c, w, v = Gtk.Grid(), Gtk.Label()
   c:add(w)

   c.property[w].left_attach = 1
   v = GObject.Value(GObject.Type.INT)
   c:child_get_property(w, 'left-attach', v)
   checkv(v.value, 1, 'number')
   v.value = 2
   c:child_set_property(w, 'left-attach', v)
   checkv(c.property[w].left_attach, 2)
   check(not pcall(function() c.property[w].notexistent = 1 end))
end

function gtk.container_add_method()
   local c, w
   c, w = Gtk.Grid(), Gtk.Label()
   c:add(w)
   check(w.parent == c)

   c, w = Gtk.Grid(), Gtk.Label()
   c:add { w, left_attach = 0, width = 2 }
   check(w.parent == c)
   checkv(c.property[w].left_attach, 0, 'number')
   checkv(c.property[w].width, 2, 'number')

   c, w = Gtk.Grid(), Gtk.Label()
   c:add(w, { left_attach = 0, width = 2 })
   check(w.parent == c)
   checkv(c.property[w].left_attach, 0, 'number')
   checkv(c.property[w].width, 2, 'number')
end

function gtk.container_add_child()
   local c, w
   c, w = Gtk.Grid(), Gtk.Label()
   c.child = w
   check(w.parent == c)

   c, w = Gtk.Grid(), Gtk.Label()
   c.child = { w, left_attach = 0, width = 2 }
   check(w.parent == c)
   checkv(c.property[w].left_attach, 0, 'number')
   checkv(c.property[w].width, 2, 'number')
end

function gtk.container_add_ctor()
   local l1, l2 = Gtk.Label(), Gtk.Label()
   local c = Gtk.Grid { { l1, width = 2 }, { l2, height = 3 } }
   check(l1.parent == c)
   check(l2.parent == c)
   checkv(c.property[l1].width, 2, 'number')
   checkv(c.property[l2].height, 3, 'number')
end

function gtk.container_child_find()
   local l1, l2 = Gtk.Label { id = 'id_l1' }, Gtk.Label { id = 'id_l2' }
   local c = Gtk.Grid {
      { l1, width = 2 },
      Gtk.Grid { id = 'in_g', { l2, height = 3 } }
   }

   check(c.child.id_l1 == l1)
   check(c.child.id_l2 == l2)
   check(c.child.id_l2.parent == c.child.in_g)
   check(c.child.notexistent == nil)
end

local uidef = [[
<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <!-- interface-requires gtk+ 3.0 -->
  <object class="GtkWindow" id="window1">
    <property name="can_focus">False</property>
    <child>
      <object class="GtkGrid" id="grid1">
	<property name="visible">True</property>
	<property name="can_focus">False</property>
	<child>
	  <object class="GtkToolbar" id="toolbar1">
	    <property name="visible">True</property>
	    <property name="can_focus">False</property>
	  </object>
	  <packing>
	    <property name="left_attach">0</property>
	    <property name="top_attach">0</property>
	    <property name="width">1</property>
	    <property name="height">1</property>
	  </packing>
	</child>
	<child>
	  <object class="GtkLabel" id="label1">
	    <property name="visible">True</property>
	    <property name="can_focus">False</property>
	    <property name="hexpand">True</property>
	    <property name="vexpand">True</property>
	    <property name="label" translatable="yes">label</property>
	  </object>
	  <packing>
	    <property name="left_attach">0</property>
	    <property name="top_attach">1</property>
	    <property name="width">1</property>
	    <property name="height">1</property>
	  </packing>
	</child>
	<child>
	  <object class="GtkStatusbar" id="statusbar1">
	    <property name="visible">True</property>
	    <property name="can_focus">False</property>
	    <property name="orientation">vertical</property>
	    <property name="spacing">2</property>
	  </object>
	  <packing>
	    <property name="left_attach">0</property>
	    <property name="top_attach">2</property>
	    <property name="width">1</property>
	    <property name="height">1</property>
	  </packing>
	</child>
      </object>
    </child>
  </object>
</interface>
]]

function gtk.builder_add_from_string()
   local b = Gtk.Builder()
   local res, e1, e2 = b:add_from_string('syntax error')
   check(not res and type(e1) == 'string')
   res, e1, e2 = b:add_from_string(uidef)
   check(res and not e1 and not e2)
   check(b:get_object('window1'))
end

function gtk.builder_add_objects_from_string()
   local b = Gtk.Builder()
   check(b:add_objects_from_string(uidef, -1, { 'statusbar1', 'label1' }))
   check(b:get_object('statusbar1') and b:get_object('label1'))
   check(not b:get_object('window1') and not b:get_object('toolbar1'))
end

function gtk.builder_add_from_file()
   local tempname = os.tmpname()
   local tempfile = io.open(tempname, 'w+')
   tempfile:write(uidef)
   tempfile:close()
   local b = Gtk.Builder()
   local res, e1, e2 = b:add_from_string('syntax error')
   check(not res and type(e1) == 'string')
   res, e1, e2 = b:add_from_file(tempname)
   check(res and not e1 and not e2)
   check(b:get_object('window1'))
   os.remove(tempname)
end

function gtk.builder_add_objects_from_file()
   local tempname = os.tmpname()
   local tempfile = io.open(tempname, 'w+')
   tempfile:write(uidef)
   tempfile:close()
   local b = Gtk.Builder()
   check(b:add_objects_from_file(tempname, { 'statusbar1', 'label1' }))
   check(b:get_object('statusbar1') and b:get_object('label1'))
   check(not b:get_object('window1') and not b:get_object('toolbar1'))
   os.remove(tempname)
end

function gtk.builder_objects()
   local builder = Gtk.Builder()
   check(builder:add_from_string(uidef))
   check(builder.objects.window1 == builder:get_object('window1'))
   check(builder.objects.statusbar1 == builder:get_object('statusbar1'))
   check(not builder.objects.notexistent)
end

function gtk.text_tag_table_ctor()
   local t1, t2 = Gtk.TextTag { name = 'tag1' }, Gtk.TextTag { name = 'tag2' }
   local t = Gtk.TextTagTable { t1, t2 }
   check(t:lookup('tag1') == t1)
   check(t:lookup('tag2') == t2)
   check(t:lookup('notexist') == nil)
end

function gtk.text_tag_table_tag()
   local t1, t2 = Gtk.TextTag { name = 'tag1' }, Gtk.TextTag { name = 'tag2' }
   local t = Gtk.TextTagTable { t1, t2 }
   check(t.tag.tag1 == t1)
   check(t.tag.tag2 == t2)
   check(t.tag.notexist == nil)
end

function gtk.liststore()
   local cols = { int = 1, string = 2 }
   local store = Gtk.ListStore.new { GObject.Type.INT, GObject.Type.STRING }
   local first = store:insert(0, { [cols.int] = 42, [cols.string] = 'hello' })
   checkv(store:get_value(first, cols.int - 1).value, 42, 'number')
   checkv(store[first][cols.int], 42, 'number')
   checkv(store:get_value(first, cols.string - 1).value, 'hello', 'string')
   checkv(store[first][cols.string], 'hello', 'string')
   store[first] = { [cols.string] = 'changed' }
   checkv(store[first][cols.string], 'changed', 'string')
   checkv(store[first][cols.int], 42, 'number')
   store[first][cols.int] = 16
   checkv(store[first][cols.string], 'changed', 'string')
   checkv(store[first][cols.int], 16, 'number')
end

function gtk.treestore()
   local cols = { int = 1, string = 2 }
   local store = Gtk.TreeStore.new { GObject.Type.INT, GObject.Type.STRING }
   local first = store:insert(
      nil, 0, { [cols.int] = 42, [cols.string] = 'hello' })
   checkv(store:get_value(first, cols.int - 1).value, 42, 'number')
   checkv(store[first][cols.int], 42, 'number')
   checkv(store:get_value(first, cols.string - 1).value, 'hello', 'string')
   checkv(store[first][cols.string], 'hello', 'string')
   store[first] = { [cols.string] = 'changed' }
   checkv(store[first][cols.string], 'changed', 'string')
   checkv(store[first][cols.int], 42, 'number')
   store[first][cols.int] = 16
   checkv(store[first][cols.string], 'changed', 'string')
   checkv(store[first][cols.int], 16, 'number')
end

function gtk.treemodel_pairs()
   local cols = { int = 1, string = 2 }
   local store = Gtk.TreeStore.new { GObject.Type.INT, GObject.Type.STRING }
   local first = store:append(
      nil, { [cols.int] = 42, [cols.string] = 'hello' })
   local sub1 = store:append(
      first, { [cols.int] = 100, [cols.string] = 'sub1' })
   local sub2 = store:append(
      first, { [cols.int] = 101, [cols.string] = 'sub2' })

   local count = 0
   for i, item in store:pairs() do
      count = count + 1
      check(Gtk.TreeIter:is_type_of(i))
      check(item[cols.string] == 'hello')
   end
   check(count == 1)

   count = 0
   for i, item in store:pairs(first) do
      count = count + 1
      check(Gtk.TreeIter:is_type_of(i))
      if (count == 1) then
	 check(item[cols.string] == 'sub1')
      else
	 check(item[cols.string] == 'sub2')
      end
   end
   check(count == 2)

   count = 0
   for i, item in store:pairs(sub1) do
      count = count + 1
   end
   check(count == 0)
end

function gtk.treeview()
   local cols = { int = 1, string = 2 }
   local store = Gtk.TreeStore.new { GObject.Type.INT, GObject.Type.STRING }
   local view = Gtk.TreeView {
      model = store,
      Gtk.TreeViewColumn {
	 { Gtk.CellRendererText {}, { text = cols.int } },
	 { Gtk.CellRendererText {}, expand = true, pack = 'end',
	   function(column, cell, model, iter)
	      return model[iter][cols.string]:toupper()
	   end },
      }
   }

   -- Unfortunately, there is no sane way to test the real contents of
   -- the treeview above, namely contents of the column, except
   -- dislaying and inspecting the tree visually, which is
   -- inappropriate for automated test.
end
