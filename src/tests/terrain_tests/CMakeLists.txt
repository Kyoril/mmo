mmo_add_test(terrain_tests
	base
	math)

# NOTE: test_terrain.cpp is wrapped in `#if false` in its entirety and contributes no test
# cases. Enabling it needs a Scene and a MaterialManager, so it would have to move to a
# client/editor-gated suite first. Kept here, visible, rather than quietly deleted.

# Cases that need a real Terrain (a Scene, a MaterialManager and a null graphics device) only
# exist where the terrain library is built; the headless server build compiles them to nothing.
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_link_libraries(terrain_tests
		terrain
		scene_graph
		graphics
		graphics_null
		frame_ui
		client_data
		tex
		tex_v1_0
		binary_io_hdrs
		log
		${OPENSSL_LIBRARIES})
	if (WIN32)
		target_link_libraries(terrain_tests graphics_d3d11)
	endif()
	target_compile_definitions(terrain_tests PRIVATE MMO_TERRAIN_TESTS_WITH_SCENE)
endif()
