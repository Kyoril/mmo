
# Bot decision making: the utility priority queue engine, the definitions it is built out of,
# and the strategies that drive an autonomous bot. Depends on bot_core for the world it acts on.
add_lib_recurse(bot_ai)
target_link_libraries(bot_ai
	bot_core
	base
	log
	game
	math
	proto_data
)

set_property(TARGET bot_ai PROPERTY FOLDER "shared")
