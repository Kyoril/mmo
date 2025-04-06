# This file, when included, does some config options for Microsoft Visual Studio.

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Make sure Visual Studio 2017 or newer is used
if (MSVC_VERSION LESS 1910)
	message(FATAL_ERROR "Visual Studio 2017 or newer is required in order to build this project! Please upgrade.")
endif()

# Make sure 64 bit build is used
if(NOT "${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
    message(FATAL_ERROR "This project needs to be built using a 64 bit compiler! Please use Win64 Visual Studio build options.")
endif()

# Enable solution folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Enable c++17 features
add_definitions("/std:c++latest")
add_compile_options(/arch:SSE2)

# If in release mode, enable optimizations and link time code generation
add_compile_options(/Zi)
add_link_options(/DEBUG)

# We want to use Vista or later API since we need this for
# GetTickCount64 which is not available on XP and prior
add_definitions("-D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN")
add_definitions("-D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING")

# Disable certain warnings, enable multi-cpu builds and allow big precompiled headers
add_definitions("/D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4800 /MP /D_WINSOCK_DEPRECATED_NO_WARNINGS /bigobj /Gy")

# Disable MIN and MAX macros for windows
add_definitions("-DNOMINMAX")
