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
find_package(OpenSSL)
if (NOT OPENSSL_FOUND)
	message(FATAL_ERROR "Couldn't find OpenSSL libraries on your system. If you're on windows, please download and install Win64 OpenSSL v1.0.2o from this website: https://slproweb.com/products/Win32OpenSSL.html!")
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

if (MMO_BUILD_TOOLS)
	find_package(FBX)
endif()