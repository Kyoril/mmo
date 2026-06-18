enable_testing()

include_directories(../mmo_bot)

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
	game_server
	assets
	proto_data
	nav_mesh)

target_sources(unit_tests PRIVATE
	../mmo_bot/bot_startup_selection.cpp
	../mmo_bot/bot_console_prompt.cpp
	../mmo_bot/bot_nav_service.cpp
	../mmo_bot/bot_movement_controller.cpp
	../mmo_bot/bot_movement_math.cpp
	../mmo_bot/bot_follow_policy.cpp
	../mmo_bot/bot_companion_state_machine.cpp
	../mmo_bot/bot_context.cpp
	../mmo_bot/bot_realm_connector.cpp
	../mmo_bot/bot_object_manager.cpp
	../mmo_bot/bot_unit.cpp
	../mmo_bot/bot_warrior_capabilities.cpp
	../mmo_bot/bot_warrior_controller.cpp
	../mmo_bot/bot_cleric_capabilities.cpp
	../mmo_bot/bot_cleric_controller.cpp
	../mmo_bot/bot_mage_capabilities.cpp
	../mmo_bot/bot_mage_controller.cpp
	../mmo_bot/bot_actions/follow_leader_action.cpp
	../mmo_bot/bot_actions/companion_follow_action.cpp
	test_bot_realm_spell_state.cpp
	test_bot_party_follow_profile.cpp
	test_bot_context.cpp
	test_bot_companion_runtime.cpp
	test_bot_follow_runtime.cpp
	test_bot_movement_controller.cpp
	test_bot_warrior_runtime.cpp
	test_bot_warrior_spellbook.cpp
	test_bot_warrior_controller.cpp
	test_bot_cleric_spellbook.cpp
	test_bot_cleric_controller.cpp
	test_bot_cleric_runtime.cpp
	test_bot_mage_spellbook.cpp
	test_bot_mage_controller.cpp
	test_bot_mage_runtime.cpp
)

if (WIN32)
	target_link_libraries(unit_tests graphics_d3d11)
endif()
	
target_link_libraries(unit_tests ${OPENSSL_LIBRARIES})
set_property(TARGET unit_tests PROPERTY FOLDER "tests")

add_test(NAME unit_tests COMMAND unit_tests)
