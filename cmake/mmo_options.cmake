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

# TODO: Add more option-specific settings here as you need
