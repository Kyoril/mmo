
# Add default executable
add_exe(login_server)
target_link_libraries(login_server base log simple_file_format_hdrs binary_io_hdrs network_hdrs sql_wrapper mysql_wrapper auth_protocol)
target_link_libraries(login_server ${OPENSSL_LIBRARIES})
set_property(TARGET login_server PROPERTY FOLDER "servers")
