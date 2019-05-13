# Add library project
add_lib(http_client)
target_link_libraries(http_client base64)
set_property(TARGET http_client PROPERTY FOLDER "shared")
