enable_testing()

add_exe(login_server_tests)

target_sources(login_server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../login_server/login_http_handlers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../login_server/player.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../login_server/player_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../login_server/realm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../login_server/realm_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../login_server/database.cpp
)

target_link_libraries(login_server_tests
    base
    log
    auth_protocol
    web_services
    http
    binary_io_hdrs
    network_hdrs
)
target_link_libraries(login_server_tests ${OPENSSL_LIBRARIES})
set_property(TARGET login_server_tests PROPERTY FOLDER "tests")
add_test(NAME login_server_tests COMMAND login_server_tests)
