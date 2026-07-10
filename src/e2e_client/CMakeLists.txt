
add_exe(e2e_client)

target_link_libraries(e2e_client
	base
	log
	simple_file_format_hdrs
	binary_io_hdrs
	network_hdrs
	auth_protocol
	game_protocol
	game
	math
	assets
	proto_data
	nav_mesh
	zlibstatic
	${OPENSSL_LIBRARIES}
)

set_property(TARGET e2e_client PROPERTY FOLDER "tests")
