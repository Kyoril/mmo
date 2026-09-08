
# Add shared graphics library
add_lib_recurse(game)

# These have always been used by the sources in here, but were never declared: every
# consumer listed them itself and got away with it on MSVC, which resolves a static library
# no matter where it sits on the link line. GNU ld does not -- with no dependency declared,
# CMake is free to emit libbase.a ahead of libgame.a and the link then fails on whichever
# game object happens to pull a base symbol in (see bot_ai_tests and ChunkReader).
target_link_libraries(game
	base
	log
	math
	assets
	binary_io_hdrs
)

if (APPLE)
    target_link_libraries(game "-framework CoreFoundation")
endif()

# Put the library into the shared solution folder
set_property(TARGET game PROPERTY FOLDER "shared")
