# Find the Lua 5.1 includes and library
#
# LUA51_INCLUDE_DIR - where to find lua.h
# LUA51_LIBRARIES - List of fully qualified libraries to link against
# LUA51_FOUND - Set to TRUE if found

# Copyright (c) 2007, Pau Garcia i Quiles, <pgquiles@elpauer.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

IF(LUA_INCLUDE_DIR AND LUA_LIBRARY)
	SET(LUA_FIND_QUIETLY TRUE)
ENDIF(LUA_INCLUDE_DIR AND LUA_LIBRARY)

FIND_PATH(LUA_INCLUDE_DIR lua.h)

SET(LUA_NAMES luajit luajit luajit-5.1 lua)
FIND_LIBRARY(LUA_LIBRARY NAMES ${LUA_NAMES})

IF(LUA_INCLUDE_DIR AND LUA_LIBRARY)
	SET(LUA_FOUND TRUE)
	INCLUDE(CheckLibraryExists)
	CHECK_LIBRARY_EXISTS(${LUA_LIBRARY} lua_close "" LUA_NEED_PREFIX)
	CHECK_LIBRARY_EXISTS(${LUA_LIBRARY} luaL_argerror "" LUA_NEED_PREFIX_)
ELSE(LUA_INCLUDE_DIR AND LUA_LIBRARY)
	SET(LUA_FOUND FALSE)
ENDIF (LUA_INCLUDE_DIR AND LUA_LIBRARY)

IF(LUA_FOUND)
	IF (NOT LUA_FIND_QUIETLY)
		MESSAGE(STATUS "Found Lua library: ${LUA_LIBRARY}")
		MESSAGE(STATUS "Found Lua headers: ${LUA_INCLUDE_DIR}")
	ENDIF (NOT LUA_FIND_QUIETLY)
ELSE(LUA_FOUND)
	IF(LUA_FIND_REQUIRED)
		MESSAGE(FATAL_ERROR "Could NOT find Lua")
	ENDIF(LUA_FIND_REQUIRED)
ENDIF(LUA_FOUND)

MARK_AS_ADVANCED(LUA_INCLUDE_DIR LUA_LIBRARY)