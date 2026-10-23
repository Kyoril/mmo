# Add library project
add_lib(https_client)
target_link_libraries(https_client base64)
if (WIN32)
	# connection.cpp loads trusted certificates from the Windows certificate store.
	target_link_libraries(https_client crypt32)
endif()
set_property(TARGET https_client PROPERTY FOLDER "shared")
