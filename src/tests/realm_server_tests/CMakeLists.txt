mmo_add_test(realm_server_tests
	base
	log
	game
	game_server
	game_protocol
	auth_protocol
	math
	network_hdrs
	binary_io_hdrs
	proto_data
	${OPENSSL_LIBRARIES})

# Only the subsystems under test are compiled in. player.cpp, mysql_database.cpp,
# program.cpp and world*.cpp are deliberately left out to keep the suite headless and free
# of MySQL and network link-time requirements.
target_sources(realm_server_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../realm_server/database.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../realm_server/player_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../realm_server/motd_manager.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../realm_server/friend_mgr.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../realm_server/player_group.cpp
)

target_include_directories(realm_server_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../..
)
