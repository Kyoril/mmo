
# Add default executable
add_exe(realm_server)
target_link_libraries(realm_server assets base log simple_file_format_hdrs binary_io_hdrs network_hdrs sql_wrapper mysql_wrapper auth_protocol game_protocol game game_server math web_services proto_data virtual_dir)
target_link_libraries(realm_server ${OPENSSL_LIBRARIES})
set_property(TARGET realm_server PROPERTY FOLDER "servers")
