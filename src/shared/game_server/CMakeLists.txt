
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Add shared graphics library
add_lib_recurse(game_server)
target_link_libraries(game_server base log game proto_data nav_mesh game_protocol)

if (APPLE)
    target_link_libraries(game_server "-framework CoreFoundation")
endif()

# Put the library into the shared solution folder
set_property(TARGET game_server PROPERTY FOLDER "shared")
