
# Add default executable
add_exe(world_server)
target_link_libraries(world_server base log assets simple_file_format_hdrs binary_io_hdrs network_hdrs sql_wrapper mysql_wrapper auth_protocol game_protocol math game game_server game_common virtual_dir libprotobuf proto_data zlibstatic)
target_link_libraries(world_server ${OPENSSL_LIBRARIES})
target_link_libraries(world_server lua luabind)
set_property(TARGET world_server PROPERTY FOLDER "servers")
