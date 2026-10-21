mmo_add_test(game_client_tests
	base
	log
	math
	game
	binary_io_hdrs
	network_hdrs
	${OPENSSL_LIBRARIES})

# Compiled in directly rather than linking game_client, whose transitive frame_ui and
# graphics dependencies do not exist in a headless build. These files have no render
# dependencies of their own.
target_sources(game_client_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/remote_movement_queue.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/remote_movement_renderer.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/attack_error_voice_gate.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/world_text_layout.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/object_update_skip.cpp
)

# test_console_var.cpp reaches into the client executable's headers; it supplies its own
# ConsoleVar constructor, so only the include path is needed, not the sources.
target_include_directories(game_client_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/mmo_client
)

# The CrossfadingSoundLoop, ambient sound and spell text tests exercise sources which need the
# client_data protobuf library -- only built alongside the client or the editor.
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_sources(game_client_tests PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/sound_entry_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/ambient_sound_emitters.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_resolver.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_voice_gate.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/combat_sound_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/spell_text_formatter.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../shared/game_client/trade_skill_list.cpp
	)
	target_link_libraries(game_client_tests client_data)
else()
	set_source_files_properties(
		${CMAKE_CURRENT_SOURCE_DIR}/test_crossfading_sound_loop.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_ambient_sound_emitters.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_resolver.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_voice_gate.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_combat_sound_player.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_spell_text_formatter.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/test_trade_skill_list.cpp
		PROPERTIES HEADER_FILE_ONLY TRUE
	)
endif()
