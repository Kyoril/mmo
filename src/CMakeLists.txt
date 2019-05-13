
# Add shared directory as include directory as well as subdirectory for project parsing
include_directories(shared)
add_subdirectory(shared)

# Add login server directory
add_subdirectory(login_server)
add_subdirectory(realm_server)
add_subdirectory(world_server)

add_subdirectory(mmo_client)

add_subdirectory(hpak_tool)