# game_server provides PersistentPetData, the payload of the realm <-> world pet packets.
mmo_add_test(auth_protocol_tests
	base
	log
	auth_protocol
	game_server
	binary_io_hdrs
	network_hdrs
	${OPENSSL_LIBRARIES})
