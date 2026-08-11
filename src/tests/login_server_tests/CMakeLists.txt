mmo_add_test(login_server_tests
	base
	log
	auth_protocol
	web_services
	http
	binary_io_hdrs
	network_hdrs
	${OPENSSL_LIBRARIES})

# The subsystems under test are compiled straight into the suite: the login server is an
# executable, so there is no library to link against.
target_sources(login_server_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../login_server/login_http_handlers.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../login_server/player.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../login_server/player_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../login_server/realm.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../login_server/realm_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../login_server/database.cpp
)
