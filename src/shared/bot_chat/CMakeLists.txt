# Language model driven chat for bots: who says what and when, the spend cap, and the client
# for the model behind it. Knows nothing about sessions; the host adapts a bot to it.
add_lib(bot_chat)
target_link_libraries(bot_chat
	https_client
	base
	log
	${OPENSSL_LIBRARIES}
)

set_property(TARGET bot_chat PROPERTY FOLDER "shared")
