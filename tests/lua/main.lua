#!/usr/bin/lua
--[[###########################################################################
#                                                                             #
# libloc - A library to determine the location of someone on the Internet     #
#                                                                             #
# Copyright (C) 2024 IPFire Development Team <info@ipfire.org>                #
#                                                                             #
# This library is free software; you can redistribute it and/or               #
# modify it under the terms of the GNU Lesser General Public                  #
# License as published by the Free Software Foundation; either                #
# version 2.1 of the License, or (at your option) any later version.          #
#                                                                             #
# This library is distributed in the hope that it will be useful,             #
# but WITHOUT ANY WARRANTY; without even the implied warranty of              #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU           #
# Lesser General Public License for more details.                             #
#                                                                             #
############################################################################--]]

luaunit = require("luaunit")

function test_load()
	-- Try loading the module
	location = require("location")

	-- Print the version
	print(location.version())
end

os.exit(luaunit.LuaUnit.run())
