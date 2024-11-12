
# Add shared graphics library
add_lib(game_client)
target_link_libraries(game_client game client_data frame_ui)

if (APPLE)
    target_link_libraries(game_client "-framework CoreFoundation")
endif()

# Put the library into the shared solution folder
set_property(TARGET game_client PROPERTY FOLDER "shared")
