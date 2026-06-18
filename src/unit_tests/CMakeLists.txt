enable_testing()

# Add default executable
add_exe(unit_tests)

# Add remote_movement_queue.cpp directly rather than linking game_client, because
# game_client -> frame_ui -> graphics pulls render dependencies that break the
# headless unit_tests build. Only the two files needed for RemoteMovementQueue are added.
# Likewise, float_curve.cpp lives in the graphics library (only linked on Windows via
# graphics_d3d11) but has no render dependencies, so add it directly for the headless build.
target_sources(unit_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../shared/game_client/remote_movement_queue.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../shared/graphics/float_curve.cpp
)

target_link_libraries(unit_tests 
	base 
	base64 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	sql_wrapper 
	mysql_wrapper 
	auth_protocol 
	game_protocol
	hpak
	hpak_v1_0
	math
	game
	game_server)

if (WIN32)
	target_link_libraries(unit_tests graphics_d3d11)
endif()
	
target_link_libraries(unit_tests ${OPENSSL_LIBRARIES})
set_property(TARGET unit_tests PROPERTY FOLDER "tests")

add_test(NAME unit_tests COMMAND unit_tests)
