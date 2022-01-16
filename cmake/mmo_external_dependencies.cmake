# This file tries to find external dependencies and sets them up.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mmo_dependencies")
message(STATUS "Adding module path: ${CMAKE_CURRENT_SOURCE_DIR}/mmo_dependencies")

# ===============================================================================
# ASIO (standalone)
# ===============================================================================
add_definitions("-DASIO_STANDALONE -D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/asio/asio")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/asio/asio/include")


# ===============================================================================
# OPENSSL
# ===============================================================================

# Find OpenSSL
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL)
if (NOT OPENSSL_FOUND)
	message(FATAL_ERROR "Couldn't find OpenSSL libraries on your system. If you're on windows, please download and install Win64 OpenSSL v1.0.2o from this website: https://slproweb.com/products/Win32OpenSSL.html!")
endif()

if (WIN32)
	list(APPEND OPENSSL_LIBRARIES "crypt32.lib;ws2_32.lib")
	message(STATUS "Libs: ${OPENSSL_LIBRARIES}")
endif()

# Check openssl version infos (right now, we want to make sure we don't use 1.1.X, but 1.0.2)
#if (NOT OPENSSL_VERSION MATCHES "1.0.2[a-z]")
#	message(FATAL_ERROR "OpenSSL 1.0.2 is required, but ${OPENSSL_VERSION} was detected! This would lead to compile errors.")
#endif()

# Mark OpenSSL options as advanced
mark_as_advanced(LIB_EAY_DEBUG LIB_EAY_RELEASE SSL_EAY_DEBUG SSL_EAY_RELEASE OPENSSL_INCLUDE_DIR)

# Include OpenSSL include directories
include_directories(${OPENSSL_INCLUDE_DIR})
link_directories(${OPENSSL_LIBRARY_DIR})



# ===============================================================================
# MYSQL
# ===============================================================================

# Find MySQL
find_package(MYSQL REQUIRED)
include_directories(${MYSQL_INCLUDE_DIR})


# ===============================================================================
# Luabind
# ===============================================================================

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/lua")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/luabind_noboost")



# ===============================================================================
# Catch
# ===============================================================================

if (MMO_BUILD_TESTS)
	include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/catch/")
endif()


# ===============================================================================
# FBX SDK
# ===============================================================================

if (MMO_BUILD_TOOLS OR MMO_BUILD_EDITOR)
	find_package(FBX)
endif()



# ===============================================================================
# Protobuffer
# ===============================================================================

option(protobuf_BUILD_TESTS OFF)
option(protobuf_BUILD_EXAMPLES OFF)
option(protobuf_MSVC_STATIC_RUNTIME OFF)
option(protobuf_WITH_ZLIB OFF)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/protobuf/src/")



# ===============================================================================
# Protobuffer
# ===============================================================================

# If we want to build the editor, we need imgui for now
if (MMO_BUILD_EDITOR)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/examples)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui-node-editor)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui-node-editor/examples)
endif()


include_directories("${CMAKE_CURRENT_SOURCE_DIR}/deps/lodepng/")