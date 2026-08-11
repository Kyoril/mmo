# Gated on a client or editor build by src/tests/CMakeLists.txt: every test here needs a
# real scene graph and a graphics device, so unlike the other suites this one cannot run on
# the headless Linux server build.
mmo_add_test(scene_graph_tests
	base
	log
	math
	scene_graph
	graphics
	graphics_null
	client_data
	tex
	tex_v1_0
	binary_io_hdrs
	${OPENSSL_LIBRARIES})

if (WIN32)
	target_link_libraries(scene_graph_tests graphics_d3d11)
endif()
