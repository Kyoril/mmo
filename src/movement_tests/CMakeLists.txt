enable_testing()

# Add default executable
add_exe(movement_tests)

target_sources(movement_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/test_helpers/test_aabb_query.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/test_helpers/test_scene.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/test_helpers/triangle_collidable.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/test_helpers/test_game_unit.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/test_helpers/party_stubs.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/test_movement_stepup_jump.cpp
)

# mmo_client headers are needed for party stubs (symbols referenced from game_client/object_mgr)
target_include_directories(movement_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/mmo_client
)

target_link_libraries(movement_tests
	base
	log
	math
	game
	game_server
	game_client
	client_data
	scene_graph
	graphics
	graphics_null
	frame_ui
	assets
	xml_handler
	tex
	tex_v1_0
	hpak
	hpak_v1_0
	network_hdrs
	binary_io_hdrs
	simple_file_format_hdrs
	sql_wrapper
	mysql_wrapper
	auth_protocol
	game_protocol
	freetype)

if (WIN32)
	target_link_libraries(movement_tests graphics_d3d11)
endif()

set_property(TARGET movement_tests PROPERTY FOLDER "tests")

add_test(NAME movement_tests COMMAND movement_tests)
