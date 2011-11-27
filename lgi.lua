------------------------------------------------------------------------------
--
--  LGI Lua-side core.
--
--  Copyright (c) 2011 Pavel Holejsovsky
--  Licensed under the MIT license:
--  http://www.opensource.org/licenses/mit-license.php
--
------------------------------------------------------------------------------

-- This is simple forwarder to real package 'lgi/init.lua'.  Normally,
-- lgi/init.lua could suffice, but this file is needed for two
-- reasons:
-- 1) Running uninstalled, because Lua unfortunately does not contain
--    './?/init.lua' component in its package.path
-- 2) Upgrading older installations (<0.2), where lgi.lua was the only
--    installed file, it would take precedence over 'lgi/init.lua'.

return require 'lgi.init'
