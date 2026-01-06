
add_exe(mmo_bot)

target_link_libraries(mmo_bot
    base
    log
    simple_file_format_hdrs
    binary_io_hdrs
    network_hdrs
    auth_protocol
    game_protocol
    game
    math
    ${OPENSSL_LIBRARIES}
)

set_property(TARGET mmo_bot PROPERTY FOLDER "client")
