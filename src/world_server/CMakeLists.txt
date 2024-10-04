
# Add default executable
add_exe(world_server)
target_link_libraries(world_server base log simple_file_format_hdrs binary_io_hdrs network_hdrs sql_wrapper mysql_wrapper auth_protocol game_protocol math game game_server virtual_dir libprotobuf proto_data)
target_link_libraries(world_server ${OPENSSL_LIBRARIES})
set_property(TARGET world_server PROPERTY FOLDER "servers")
