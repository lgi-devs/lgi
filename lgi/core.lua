------------------------------------------------------------------------------
--
--  LGI Lua-side core loader.
--
--  Copyright (c) 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

-- This is simple forwarder to real package 'lgi/core/init.lua'.
-- Normally, lgi/core/init.lua could suffice, but standard lua
-- unfortunately does not contain './?/init.lua' component in its
-- package.path, causing failures when running uninstalled.
return require 'lgi.core.init'
