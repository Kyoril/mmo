# Add library project
add_lib(https_client)
target_link_libraries(https_client base64)
set_property(TARGET https_client PROPERTY FOLDER "shared")
