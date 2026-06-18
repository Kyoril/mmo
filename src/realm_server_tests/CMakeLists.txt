enable_testing()

add_exe(realm_server_tests)

# Compile only the subsystems under test — skip heavy server sources
# (player.cpp, mysql_database.cpp, program.cpp, world*.cpp) to keep the
# binary headless and free of MySQL/network link-time requirements.
target_sources(realm_server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../realm_server/database.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../realm_server/player_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../realm_server/motd_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../realm_server/friend_mgr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../realm_server/player_group.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_stubs.cpp
)

target_include_directories(realm_server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(realm_server_tests
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
)

target_link_libraries(realm_server_tests ${OPENSSL_LIBRARIES})
set_property(TARGET realm_server_tests PROPERTY FOLDER "tests")

add_test(NAME realm_server_tests COMMAND realm_server_tests)
