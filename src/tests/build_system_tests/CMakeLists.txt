mmo_add_test(build_system_tests)

# The suite reads the project's own CMake files, so it needs to find them no matter which
# directory ctest happens to start it in.
target_compile_definitions(build_system_tests PRIVATE
	MMO_CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
