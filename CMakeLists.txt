# We want a kind-of modern cmake
cmake_minimum_required(VERSION 3.12)

# Generate a solution file
project(mmo)

# Include output directory which will contain auto generated header files
include_directories(${PROJECT_BINARY_DIR})
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Add cmake directory to the list of paths
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Add project options
include(mmo_options)

# Include compiler-specific settings and options
include(mmo_compiler_settings)

# Look for external dependencies
include(mmo_external_dependencies)

# Include cmake macros
include(mmo_macros)

# Include the commit version info generation
include(mmo_version)

# Add include directories
include_directories(deps)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/deps/zlib)
include_directories(src)

# Add source directory
add_subdirectory(deps)
add_subdirectory(src)
