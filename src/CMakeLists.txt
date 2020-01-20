
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
endif()

if (WIN32 OR APPLE)
	add_subdirectory(launcher)
endif()
add_subdirectory(update_compiler)

add_subdirectory(hpak_tool)
add_subdirectory(texture_tool)
