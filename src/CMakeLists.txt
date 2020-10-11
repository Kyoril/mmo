
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
	if (FBXSDK_FOUND)
		add_subdirectory(model_editor)
	else()
		message(STATUS "FBX SDK not properly installed, so model_editor won't be included in project files!")
	endif()
endif()

if ((WIN32 OR APPLE) AND MMO_BUILD_LAUNCHER)
	add_subdirectory(launcher)
endif()
add_subdirectory(update_compiler)

if (MMO_BUILD_TOOLS)
	add_subdirectory(hpak_tool)
	add_subdirectory(texture_tool)
endif()

if (MMO_BUILD_TESTS)
	add_subdirectory(unit_tests)
endif()