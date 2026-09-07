
# Headless bot client: everything needed to authenticate, enter the world and act as a real
# network peer, without any renderer or asset pipeline. Shared by the e2e scenario client and
# the bot swarm host.
add_lib(bot_core)
target_link_libraries(bot_core
	base
	log
	simple_file_format_hdrs
	binary_io_hdrs
	network_hdrs
	auth_protocol
	game_protocol
	game
	math
	assets
	proto_data
	nav_mesh
	zlibstatic
	${OPENSSL_LIBRARIES}
)

set_property(TARGET bot_core PROPERTY FOLDER "shared")
