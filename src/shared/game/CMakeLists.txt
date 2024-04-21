
# Add shared graphics library
add_lib(game)
target_link_libraries(game assets proto_data client_data)

if (APPLE)
    target_link_libraries(game "-framework CoreFoundation")
endif()

# Put the library into the shared solution folder
set_property(TARGET game PROPERTY FOLDER "shared")
