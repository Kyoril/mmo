
# Add shared graphics library
add_lib(game_server)
target_link_libraries(game_server game proto_data)

if (APPLE)
    target_link_libraries(game_server "-framework CoreFoundation")
endif()

# Put the library into the shared solution folder
set_property(TARGET game_server PROPERTY FOLDER "shared")
