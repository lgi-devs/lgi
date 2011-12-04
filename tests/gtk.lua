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
