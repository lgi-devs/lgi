------------------------------------------------------------------------------
--
--  lgi GooCanvas override module.
--
--  Copyright (c) 2017 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

local select, type, pairs, setmetatable, error
   = select, type, pairs, setmetatable, error
local lgi = require 'lgi'
local Goo = lgi.GooCanvas


Goo.Canvas._attribute = {
   root_item = Goo.Canvas.get_root_item,
   root_item_model = Goo.Canvas.get_root_item_model,
   static_root_item = Goo.Canvas.get_static_root_item,
   static_root_item_model = Goo.Canvas.get_static_root_item_model,
}

-- Remove 'parent' field from implementation classes because it
-- clashes with 'parent' field defined in implementations of
-- CanvasItem interface.
for _, class in pairs {
   Goo.CanvasEllipse, Goo.CanvasGrid, Goo.CanvasGroup, Goo.CanvasImage,
   Goo.CanvasItemSimple, Goo.CanvasPath, Goo.CanvasPolyline,
   Goo.CanvasRect, Goo.CanvasTable, Goo.CanvasText, Goo.CanvasWidget,
} do
   local _ = class._field.parent
   class._field.parent = nil
end
