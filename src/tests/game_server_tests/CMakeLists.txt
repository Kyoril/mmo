mmo_add_test(game_server_tests
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
	game_server
	http_client
	https_client
	${OPENSSL_LIBRARIES})

# The world server is an executable; its self-contained bug report pieces are compiled in here.
target_sources(game_server_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/world_server/bug_report_uploader.cpp
	${CMAKE_SOURCE_DIR}/src/world_server/bug_report_transport.cpp
	${CMAKE_SOURCE_DIR}/src/world_server/bug_report_document.cpp
)
