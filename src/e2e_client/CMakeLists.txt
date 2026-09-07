
add_exe(e2e_client)

target_link_libraries(e2e_client
	bot_core
	base
	log
	game_protocol
	game
	math
	lua
	luabind
)

set_property(TARGET e2e_client PROPERTY FOLDER "tests")
