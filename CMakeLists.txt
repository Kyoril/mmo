# We want a kind-of modern cmake
cmake_minimum_required(VERSION 3.12)

# Generate a solution file
project(mmo)

cmake_policy(SET CMP0169 OLD)

include(FetchContent)

FetchContent_Declare(
  protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG        v3.21.12
)

FetchContent_GetProperties(protobuf)
if(NOT protobuf_POPULATED)
  FetchContent_Populate(protobuf)

  # Set the build options for Protobuf
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
  set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

  # Add the fetched Google Protobuf as a subdirectory
  add_subdirectory(${protobuf_SOURCE_DIR} ${protobuf_BINARY_DIR})
  
  set_property(TARGET libprotobuf PROPERTY FOLDER "deps")
  set_property(TARGET libprotobuf-lite PROPERTY FOLDER "deps")
  set_property(TARGET libprotoc PROPERTY FOLDER "deps")
  set_property(TARGET protoc PROPERTY FOLDER "deps")
endif()

set(Protobuf_INCLUDE_DIR ${protobuf_SOURCE_DIR}/src)

# Include output directory which will contain auto generated header files
include_directories(${PROJECT_BINARY_DIR})
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${Protobuf_INCLUDE_DIR})

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

add_definitions("-DDT_POLYREF64")
add_definitions("-DXML_STATIC")
add_definitions("-DUUID_SYSTEM_GENERATOR")

# Add source directory
add_subdirectory(deps)
add_subdirectory(src)
