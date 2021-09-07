# This file contains possible config options for the cmake project


# ===============================================================================
# OPTION DEFINITIONS
# ===============================================================================

# If enabled, assertions are enabled even in release builds
option(MMO_WITH_RELEASE_ASSERTS "Enables assertions in release builds." OFF)

# If enabled, multiple c++ files are batched together for building. This might reduce build times but should not be used for developing
# but rather for distribution builds.
option(MMO_UNITY_BUILD "combine cpp files to accelerate compilation" OFF)
mark_as_advanced(MMO_UNITY_BUILD)

# The number of c++ files to be batched together when unity build is enabled.
set(MMO_UNITY_FILES_PER_UNIT "50" CACHE STRING "Number of files per compilation unit in Unity build.")
mark_as_advanced(MMO_UNITY_FILES_PER_UNIT)

# Whether developer commands will be available for the servers (things like create item for players, which you don't want
# on a public server to avoid GM power abuse, but which you might need on development servers to test certain things quickly).
option(MMO_WITH_DEV_COMMANDS "If checked, developer commands will be available." OFF)

# If enabled, unit tests will be built.
option(MMO_BUILD_TESTS "If checked, will try to test programs." ON)

# If enabled, unit tests will be built.
set(MMO_SRP6_N "894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7" CACHE STRING "Hex representation of a prime number for srp6a calculations.")
set(MMO_SRP6_g "07" CACHE STRING "Hex representation of a prime number for srp6a calculations.")

# TODO: Add more options here as you need


# ===============================================================================
# OPTION APPLICATION
# ===============================================================================

if(MMO_WITH_DEV_COMMANDS)
	add_definitions("-DMMO_WITH_DEV_COMMANDS")
endif(MMO_WITH_DEV_COMMANDS)

if (MMO_WITH_RELEASE_ASSERTS)
	add_definitions("-DMMO_ALWAYS_ASSERT")
endif()

if (MMO_BUILD_TESTS)
	enable_testing()
	add_definitions("-DMMO_BUILD_TESTS=1")
else()
	add_definitions("-DMMO_BUILD_TESTS=0")
endif()

# TODO: Add more option-specific settings here as you need

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/config.h @ONLY)

# Advanced default cmake options
mark_as_advanced(EXECUTABLE_OUTPUT_PATH LIBRARY_OUTPUT_PATH)

# Advanced mmo options
mark_as_advanced(MMO_SRP6_g MMO_SRP6_N)