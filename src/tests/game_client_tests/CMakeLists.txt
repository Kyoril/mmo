mmo_add_test(game_client_tests
	base
	log
	math
	game
	binary_io_hdrs
	network_hdrs
	${OPENSSL_LIBRARIES})

# Compiled in directly rather than linking game_client, whose transitive frame_ui and
# graphics dependencies do not exist in a headless build. These two files have no render
# dependencies of their own.
target_sources(game_client_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/remote_movement_queue.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/remote_movement_renderer.cpp
)

# test_console_var.cpp reaches into the client executable's headers; it supplies its own
# ConsoleVar constructor, so only the include path is needed, not the sources.
target_include_directories(game_client_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/mmo_client
)

# The CrossfadingSoundLoop tests exercise sound_entry_player.cpp, which needs the
# client_data protobuf library -- only built alongside the client or the editor.
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_sources(game_client_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/sound_entry_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_resolver.cpp
	)
	target_link_libraries(game_client_tests client_data)
else()
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/test_crossfading_sound_loop.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_resolver.cpp
		PROPERTIES HEADER_FILE_ONLY TRUE
	)
endif()
