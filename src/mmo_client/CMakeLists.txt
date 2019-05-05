
# Add executable
add_gui_exe(mmo_client)

target_link_libraries(mmo_client base log simple_file_format_hdrs binary_io_hdrs network_hdrs auth_protocol)
target_link_libraries(mmo_client ${OPENSSL_LIBRARIES})
set_property(TARGET mmo_client PROPERTY FOLDER "client")
