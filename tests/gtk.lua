--[[--------------------------------------------------------------------------

  LGI testsuite, Gtk overrides test group.

  Copyright (c) 2010, 2011 Pavel Holejsovsky
  Licensed under the MIT license:
  http://www.opensource.org/licenses/mit-license.php

--]]--------------------------------------------------------------------------

local io = require 'io'
local os = require 'os'
local lgi = require 'lgi'

local check = testsuite.check
local checkv = testsuite.checkv
local gtk = testsuite.group.new('gtk')

function gtk.widget_style()
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
   local w = Gtk.ProgressBar()
   local v = GObject.Value(GObject.Type.INT)
   w:style_get_property('xspacing', v)
   checkv(w.style.xspacing, v.value, 'number')
   check(not pcall(function() return w.style.nonexistent end))
end

function gtk.buildable_id()
   local Gtk = lgi.Gtk
   local w = Gtk.Label()
   checkv(w.id, nil, nil)
   w.id = 'label_id'
   checkv(w.id, 'label_id', 'string')
end

function gtk.container_property()
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
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
   local Gtk = lgi.Gtk
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
   local Gtk = lgi.Gtk
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
   local Gtk = lgi.Gtk
   local l1, l2 = Gtk.Label(), Gtk.Label()
   local c = Gtk.Grid { { l1, width = 2 }, { l2, height = 3 } }
   check(l1.parent == c)
   check(l2.parent == c)
   checkv(c.property[l1].width, 2, 'number')
   checkv(c.property[l2].height, 3, 'number')
end

function gtk.container_child_find()
   local Gtk = lgi.Gtk
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
   local Gtk = lgi.Gtk
   local b = Gtk.Builder()
   local res, err = b:add_from_string('syntax error')
   check(not res and lgi.GLib.Error:is_type_of(err))
   res, err = b:add_from_string(uidef)
   check(res and not err)
   check(b:get_object('window1'))
end

function gtk.builder_add_objects_from_string()
   local Gtk = lgi.Gtk
   local b = Gtk.Builder()
   check(b:add_objects_from_string(uidef, -1, { 'statusbar1', 'label1' }))
   check(b:get_object('statusbar1') and b:get_object('label1'))
   check(not b:get_object('window1') and not b:get_object('toolbar1'))
end

function gtk.builder_add_from_file()
   local Gtk = lgi.Gtk
   local tempname = os.tmpname()
   local tempfile = io.open(tempname, 'w+')
   tempfile:write(uidef)
   tempfile:close()
   local b = Gtk.Builder()
   local res, err = b:add_from_string('syntax error')
   check(not res and lgi.GLib.Error:is_type_of(err))
   res, err = b:add_from_file(tempname)
   check(res and not err)
   check(b:get_object('window1'))
   os.remove(tempname)
end

function gtk.builder_add_objects_from_file()
   local Gtk = lgi.Gtk
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
   local Gtk = lgi.Gtk
   local builder = Gtk.Builder()
   check(builder:add_from_string(uidef))
   check(builder.objects.window1 == builder:get_object('window1'))
   check(builder.objects.statusbar1 == builder:get_object('statusbar1'))
   check(not builder.objects.notexistent)
end

function gtk.text_tag_table_ctor()
   local Gtk = lgi.Gtk
   local t1, t2 = Gtk.TextTag { name = 'tag1' }, Gtk.TextTag { name = 'tag2' }
   local t = Gtk.TextTagTable { t1, t2 }
   check(t:lookup('tag1') == t1)
   check(t:lookup('tag2') == t2)
   check(t:lookup('notexist') == nil)
end

function gtk.text_tag_table_tag()
   local Gtk = lgi.Gtk
   local t1, t2 = Gtk.TextTag { name = 'tag1' }, Gtk.TextTag { name = 'tag2' }
   local t = Gtk.TextTagTable { t1, t2 }
   check(t.tag.tag1 == t1)
   check(t.tag.tag2 == t2)
   check(t.tag.notexist == nil)
end

function gtk.liststore()
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
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
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
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

function gtk.treeiter()
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
   local giter = Gtk.TreeIter()
   giter.user_data = giter._native
   local Model = GObject.Object:derive('LgiTestModel2', { Gtk.TreeModel })
   function Model:do_get_iter(path)
      return giter
   end
   local model = Model()
   local niter = model:get_iter(Gtk.TreePath.new_from_string('0'))
   check(giter.user_data == niter.user_data)
   check(giter ~= niter)
end

function gtk.treemodel_pairs()
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
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
   local Gtk = lgi.Gtk
   local GObject = lgi.GObject
   local cols = { int = 1, string = 2 }
   local store = Gtk.TreeStore.new { GObject.Type.INT, GObject.Type.STRING }
   local renderer = Gtk.CellRendererText { id = 'renderer' }
   local column = Gtk.TreeViewColumn {
      id = 'column',
      { renderer, { text = cols.int } },
      { Gtk.CellRendererText {}, expand = true, pack = 'end',
	function(column, cell, model, iter)
	   return model[iter][cols.string]:toupper()
	end },
   }

   local view = Gtk.TreeView {
      id = 'view',
      model = store,
      column
   }
   -- Check that column is accessible by its 'id' attribute.
   check(view.child.view == view)
   check(view.child.column == column)

   -- Check that renderer is accessible by its 'id' attribute.
   check(view.child.renderer == renderer)
end

function gtk.actiongroup_add()
   local Gtk = lgi.Gtk
   -- Adding normal action and action with an accelerator.
   local ag = Gtk.ActionGroup()
   local a1, a2 = Gtk.Action { name = 'a1' }, Gtk.Action { name = 'a2' }
   ag:add(a1)
   check(#ag:list_actions() == 1)
   check(ag:get_action('a1') == a1)
   ag:add { a2, accelerator = '<control>A' }
   check(#ag:list_actions() == 2)
   check(ag:get_action('a2') == a2)

   -- Adding a group of radio actions, this time inside the group ctor.
   local chosen
   a1 = Gtk.RadioAction { name = 'a1', value = 1 }
   a2 = Gtk.RadioAction { name = 'a2', value = 2 }
   ag = Gtk.ActionGroup {
      { a1, { a2, accelerator = '<control>a' },
	on_change = function(action) chosen = action end }
   }
   check(#ag:list_actions() == 2)
   check(ag:get_action('a1') == a1)
   check(ag:get_action('a2') == a2)
   check(chosen == nil)
   a1:activate()
   check(chosen == a1)
   a2:activate()
   check(chosen == a2)
end

function gtk.actiongroup_index()
   local Gtk = lgi.Gtk
   local a1, a2 = Gtk.Action { name = 'a1' }, Gtk.Action { name = 'a2' }
   local ag = Gtk.ActionGroup { a1, a2 }
   check(ag.action.a1 == a1)
   check(ag.action.a2 == a2)
end
