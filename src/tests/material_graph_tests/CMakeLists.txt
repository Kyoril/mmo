mmo_add_test(material_graph_tests material_graph graphics assets tex tex_v1_0 binary_io_hdrs math base log)

# graphics creates its devices by name, so the test binary needs the device implementations to link.
# The null device is enough for the graph; the Direct3D one only exists in client and editor builds.
target_link_libraries(material_graph_tests graphics_null ${OPENSSL_LIBRARIES})
if (TARGET graphics_d3d11)
	target_link_libraries(material_graph_tests graphics_d3d11)
elseif (TARGET graphics_metal)
	target_link_libraries(material_graph_tests graphics_metal)
endif()
