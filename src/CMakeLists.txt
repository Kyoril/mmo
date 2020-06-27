
# Add shared directory as include directory as well as subdirectory for project parsing
include_directories(shared)
add_subdirectory(shared)

# Add login server directory
add_subdirectory(login_server)
add_subdirectory(realm_server)
add_subdirectory(world_server)


if (MMO_BUILD_CLIENT)
	add_subdirectory(mmo_client)
endif()
if (MMO_BUILD_EDITOR)
	add_subdirectory(mmo_edit)
	add_subdirectory(model_editor)
endif()

if ((WIN32 OR APPLE) AND MMO_BUILD_LAUNCHER)
	add_subdirectory(launcher)
endif()
add_subdirectory(update_compiler)

if (MMO_BUILD_TOOLS)
	add_subdirectory(hpak_tool)
	add_subdirectory(texture_tool)
	if (FBXSDK_FOUND)
		add_subdirectory(mesh_tool)
	else()
		message(STATUS "FBX SDK not properly installed, so mesh_tool won't be included in project files!")
	endif()
endif()

if (MMO_BUILD_TESTS)
	add_subdirectory(unit_tests)
endif()