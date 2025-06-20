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

if (WIN32)
	option(MMO_BUILD_CLIENT "If checked, will try to build the client." ON)
else()
	option(MMO_BUILD_CLIENT "If checked, will try to build the client." OFF)
endif()

if (WIN32 OR APPLE)
	option(MMO_BUILD_LAUNCHER "If checked, will try to build the launcher." OFF)
endif()

option(MMO_WITH_DISCORD_RPC "If checked, will try to build the discord rpc and integrate it into the game client." OFF)
if (MMO_WITH_DISCORD_RPC)
	set(MMO_DISCORD_APP_ID "123456789" CACHE STRING "Discord application ID for the game client.")
endif()

# If enabled, the editor will be built. The editor can be used to modify static game data like creature spawns etc.
# However, it's a heavy tool with heavy dependencies and thus you might not want to build this yourself, so this option is 
# turned OFF by default.
option(MMO_BUILD_EDITOR "If checked, will try to build the editor. You will need to have QT 5 installed to build this." OFF)

# If enabled, additional tools (most likely console tools) are build for things like navmesh generation. This does not require
# additional dependencies, but is turned OFF so that the standard cmake options will simply build the servers to make things
# easier for linux builds.
option(MMO_BUILD_TOOLS "If checked, will try to build tools." OFF)

# If enabled, unit tests will be built.
option(MMO_BUILD_TESTS "If checked, will try to test programs." ON)

# If enabled, disables MSVC iterator debugging in debug builds for better performance
option(MMO_DISABLE_ITERATOR_DEBUG "If checked, disables MSVC iterator debugging in debug builds." ON)

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

if (MMO_BUILD_EDITOR)
	add_definitions("-DMMO_BUILD_EDITOR=1")
else()
	add_definitions("-DMMO_BUILD_EDITOR=0")
endif()

if (MMO_BUILD_LAUNCHER)
	add_definitions("-DMMO_BUILD_LAUNCHER=1")
else()
	add_definitions("-DMMO_BUILD_LAUNCHER=0")
endif()

if (MMO_BUILD_TOOLS)
	add_definitions("-DMMO_BUILD_TOOLS=1")
else()
	add_definitions("-DMMO_BUILD_TOOLS=0")
endif()

if (MMO_BUILD_CLIENT)
	add_definitions("-DMMO_BUILD_CLIENT=1")
else()
	add_definitions("-DMMO_BUILD_CLIENT=0")
endif()

# Apply MSVC iterator debug settings
if (MSVC AND MMO_DISABLE_ITERATOR_DEBUG)
	add_definitions("-D_ITERATOR_DEBUG_LEVEL=0")
endif()

# Function to apply iterator debug settings to a target
function(apply_iterator_debug_to_target target_name)
	if (MSVC AND MMO_DISABLE_ITERATOR_DEBUG)
		target_compile_definitions(${target_name} PRIVATE _ITERATOR_DEBUG_LEVEL=0)
	endif()
endfunction()

# TODO: Add more option-specific settings here as you need

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/config.h @ONLY)

# Advanced default cmake options
mark_as_advanced(EXECUTABLE_OUTPUT_PATH LIBRARY_OUTPUT_PATH)

# Advanced mmo options
mark_as_advanced(MMO_SRP6_g MMO_SRP6_N)