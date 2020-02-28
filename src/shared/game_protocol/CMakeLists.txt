# Add library project
add_lib(game_protocol)

# Settings
target_link_libraries(game_protocol base binary_io_hdrs network_hdrs)
set_property(TARGET game_protocol PROPERTY FOLDER "shared")
