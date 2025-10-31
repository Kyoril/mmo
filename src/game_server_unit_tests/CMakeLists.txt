enable_testing()

# Add default executable
add_exe(game_server_unit_tests)
target_link_libraries(game_server_unit_tests 
	base 
	base64 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	sql_wrapper 
	mysql_wrapper 
	auth_protocol 
	game_protocol
	hpak
	hpak_v1_0
	math
	game
	game_server)
	
target_link_libraries(game_server_unit_tests ${OPENSSL_LIBRARIES})
set_property(TARGET game_server_unit_tests PROPERTY FOLDER "tests")

add_test(NAME game_server_unit_tests COMMAND game_server_unit_tests)
