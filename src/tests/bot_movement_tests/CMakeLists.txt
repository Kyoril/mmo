mmo_add_test(bot_movement_tests
	base
	math
	binary_io_hdrs)

# The bot's movement math lives in the e2e_client executable, so its sources are compiled
# into this suite directly rather than linked from a library.
target_sources(bot_movement_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/e2e_client/bot_movement_math.cpp)

target_include_directories(bot_movement_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/e2e_client)
