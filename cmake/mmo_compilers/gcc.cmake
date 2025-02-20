# This file, when included, does some config options for GCC.

# GCC 11.0 or newer is required
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
if (NOT (GCC_VERSION VERSION_GREATER 11.0 OR GCC_VERSION VERSION_EQUAL 11.0))
	message(FATAL_ERROR "GCC 11.0 or newer is required in order to build this project, but you have ${GCC_VERSION}. Please consider an update or switch over to clang.")
endif()

# Enable C++20 standard
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
list(APPEND CMAKE_CXX_FLAGS "-std=c++2a -pthread")

add_compile_options(-msse2)

if(UNIX)
	link_libraries("uuid")
endif()

# Zlib is required for GCC builds
find_package(ZLIB REQUIRED)
