
# Runs many autonomous bots in one process against a live server stack.
add_exe(bot_swarm)

target_link_libraries(bot_swarm
	bot_ai
	bot_core
	base
	log
	game_protocol
	game
	math
	proto_data
)

set_property(TARGET bot_swarm PROPERTY FOLDER "tests")
