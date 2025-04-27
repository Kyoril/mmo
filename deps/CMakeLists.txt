
add_subdirectory(zlib)
add_subdirectory(lua)
add_subdirectory(luabind_noboost)
add_subdirectory(expat)

if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(freetype)
endif()

if (MMO_BUILD_CLIENT AND MMO_WITH_DISCORD_RPC)
	set(BUILD_EXAMPLES OFF CACHE BOOL "")
	add_subdirectory(discord-rpc)
	set_property(TARGET discord-rpc PROPERTY FOLDER "deps")
	set_property(TARGET clangformat PROPERTY FOLDER "deps")
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
set_property(TARGET zlibstatic PROPERTY FOLDER "deps")
