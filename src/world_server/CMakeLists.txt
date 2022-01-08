
# Add default executable
add_exe(world_server)
target_link_libraries(world_server base log simple_file_format_hdrs binary_io_hdrs network_hdrs sql_wrapper mysql_wrapper auth_protocol math game)
target_link_libraries(world_server ${OPENSSL_LIBRARIES})
set_property(TARGET world_server PROPERTY FOLDER "servers")
