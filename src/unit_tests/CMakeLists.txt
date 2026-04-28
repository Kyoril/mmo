enable_testing()

# Add default executable
add_exe(unit_tests)
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
	../mmo_bot/bot_movement_math.cpp
	../mmo_bot/bot_follow_policy.cpp
	../mmo_bot/bot_companion_state_machine.cpp
	../mmo_bot/bot_context.cpp
	../mmo_bot/bot_realm_connector.cpp
	../mmo_bot/bot_object_manager.cpp
	../mmo_bot/bot_unit.cpp
	../mmo_bot/bot_warrior_capabilities.cpp
	../mmo_bot/bot_warrior_controller.cpp
	../mmo_bot/bot_actions/follow_leader_action.cpp
	test_bot_warrior_runtime.cpp
	test_bot_warrior_spellbook.cpp
	test_bot_warrior_controller.cpp
)

if (WIN32)
	target_link_libraries(unit_tests graphics_d3d11)
endif()
	
target_link_libraries(unit_tests ${OPENSSL_LIBRARIES})
set_property(TARGET unit_tests PROPERTY FOLDER "tests")

add_test(NAME unit_tests COMMAND unit_tests)
