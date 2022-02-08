
add_subdirectory(zlib)
add_subdirectory(lua)
add_subdirectory(luabind_noboost)
add_subdirectory(expat)

if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(freetype)
endif()

