
add_subdirectory(zlib)
add_subdirectory(lua)
add_subdirectory(luabind_noboost)
add_subdirectory(expat)

if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(freetype)
endif()

set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")
add_subdirectory(recastnavigation)
	
set_property(TARGET Recast PROPERTY FOLDER "deps")
set_property(TARGET Detour PROPERTY FOLDER "deps")
set_property(TARGET DebugUtils PROPERTY FOLDER "deps")
set_property(TARGET DetourCrowd PROPERTY FOLDER "deps")
set_property(TARGET DetourTileCache PROPERTY FOLDER "deps")
