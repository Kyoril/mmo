mmo_add_test(content_cook_tests content_cook material_graph graphics assets tex tex_v1_0 binary_io_hdrs math base log)

# graphics creates its devices by name, so the test binary needs the device implementations to link.
target_link_libraries(content_cook_tests graphics_null ${OPENSSL_LIBRARIES})
if (TARGET graphics_d3d11)
	target_link_libraries(content_cook_tests graphics_d3d11)
elseif (TARGET graphics_metal)
	target_link_libraries(content_cook_tests graphics_metal)
endif()
