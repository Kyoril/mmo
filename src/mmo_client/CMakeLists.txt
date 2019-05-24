
# Add executable
add_gui_exe(mmo_client)

target_link_libraries(mmo_client base log simple_file_format_hdrs binary_io_hdrs network_hdrs auth_protocol graphics math virtual_dir hpak_v1_0 tex_v1_0 assets)
target_link_libraries(mmo_client ${OPENSSL_LIBRARIES})
set_property(TARGET mmo_client PROPERTY FOLDER "client")
