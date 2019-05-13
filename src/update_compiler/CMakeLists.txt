add_exe(update_compiler)
target_link_libraries(update_compiler update_compilation)
target_link_libraries(update_compiler ${OPENSSL_LIBRARIES})
set_property(TARGET update_compiler PROPERTY FOLDER "tools")
