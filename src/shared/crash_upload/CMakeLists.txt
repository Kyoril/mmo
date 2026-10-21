# Add library project
add_lib(crash_upload)
target_link_libraries(crash_upload base log http_client https_client ${OPENSSL_LIBRARIES})
set_property(TARGET crash_upload PROPERTY FOLDER "shared")
