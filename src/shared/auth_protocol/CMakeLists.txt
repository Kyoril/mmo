# Add library project
add_lib(auth_protocol)

# Settings
target_link_libraries(auth_protocol base binary_io_hdrs network_hdrs)
set_property(TARGET auth_protocol PROPERTY FOLDER "shared")
