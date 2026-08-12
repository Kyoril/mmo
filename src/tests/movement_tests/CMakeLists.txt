mmo_add_test(movement_tests
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

# mmo_client headers are needed for the party stubs, which stand in for symbols that
# game_client/object_mgr references but the client executable defines.
target_include_directories(movement_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/mmo_client
	$<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>
)

if (WIN32)
	target_link_libraries(movement_tests graphics_d3d11)
endif()
