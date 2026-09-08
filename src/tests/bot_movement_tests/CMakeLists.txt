mmo_add_test(bot_movement_tests
	base
	math
	binary_io_hdrs)

# Only the movement math is under test here, and it has no dependencies of its own, so its
# source is compiled into the suite directly rather than linking the whole bot_core library.
target_sources(bot_movement_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/shared/bot_core/bot_movement_math.cpp)

target_include_directories(bot_movement_tests PRIVATE
	${CMAKE_SOURCE_DIR}/src/shared/bot_core)
