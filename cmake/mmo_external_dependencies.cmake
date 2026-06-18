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
option(MMO_OPENSSL_STATIC "Prefer static OpenSSL linkage when available" ON)
set(OPENSSL_USE_STATIC_LIBS ${MMO_OPENSSL_STATIC})
find_package(OpenSSL REQUIRED)
if (NOT OPENSSL_FOUND)
	message(FATAL_ERROR "Couldn't find OpenSSL libraries on your system. If you're on windows, please download and install Win64 OpenSSL v1.0.2o from this website: https://slproweb.com/products/Win32OpenSSL.html!")
endif()

if (TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
	set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
else()
	if (WIN32)
		list(APPEND OPENSSL_LIBRARIES crypt32 ws2_32)
	endif()
endif()
message(STATUS "Libs: ${OPENSSL_LIBRARIES}")

# Check openssl version infos (right now, we want to make sure we don't use 1.1.X, but 1.0.2)
#if (NOT OPENSSL_VERSION MATCHES "1.0.2[a-z]")
#	message(FATAL_ERROR "OpenSSL 1.0.2 is required, but ${OPENSSL_VERSION} was detected! This would lead to compile errors.")
#endif()

# Mark OpenSSL options as advanced
mark_as_advanced(LIB_EAY_DEBUG LIB_EAY_RELEASE SSL_EAY_DEBUG SSL_EAY_RELEASE OPENSSL_INCLUDE_DIR)

# Include OpenSSL include directories
include_directories(${OPENSSL_INCLUDE_DIR})
link_directories(${OPENSSL_LIBRARY_DIR})

# Runtime DLLs that need to be deployed next to executables on Windows.
set(MMO_WINDOWS_RUNTIME_DLLS "")
# Config-dependent runtime DLLs (contain generator expressions for multi-config builds).
set(MMO_WINDOWS_RUNTIME_DLLS_GENEX "")
if (WIN32)
	set(_fmod_runtime_dll "${CMAKE_CURRENT_SOURCE_DIR}/deps/fmod/lib/x64/fmod.dll")
	if (EXISTS "${_fmod_runtime_dll}")
		list(APPEND MMO_WINDOWS_RUNTIME_DLLS "${_fmod_runtime_dll}")
	endif()

	set(_openssl_lib_dirs "")
	if (OPENSSL_CRYPTO_LIBRARY)
		get_filename_component(_openssl_crypto_lib_dir "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
		list(APPEND _openssl_lib_dirs "${_openssl_crypto_lib_dir}" "${_openssl_crypto_lib_dir}/../bin")
	endif()
	if (OPENSSL_SSL_LIBRARY)
		get_filename_component(_openssl_ssl_lib_dir "${OPENSSL_SSL_LIBRARY}" DIRECTORY)
		list(APPEND _openssl_lib_dirs "${_openssl_ssl_lib_dir}" "${_openssl_ssl_lib_dir}/../bin")
	endif()
	if (OPENSSL_ROOT_DIR)
		list(APPEND _openssl_lib_dirs "${OPENSSL_ROOT_DIR}/bin")
	endif()
	list(REMOVE_DUPLICATES _openssl_lib_dirs)

	find_file(OPENSSL_SSL_RUNTIME_DLL
		NAMES libssl-3-x64.dll libssl-3.dll libssl-1_1-x64.dll libssl-1_1.dll ssleay32.dll
		PATHS ${_openssl_lib_dirs}
		NO_DEFAULT_PATH
	)
	find_file(OPENSSL_CRYPTO_RUNTIME_DLL
		NAMES libcrypto-3-x64.dll libcrypto-3.dll libcrypto-1_1-x64.dll libcrypto-1_1.dll libeay32.dll
		PATHS ${_openssl_lib_dirs}
		NO_DEFAULT_PATH
	)
	if (OPENSSL_SSL_RUNTIME_DLL)
		list(APPEND MMO_WINDOWS_RUNTIME_DLLS "${OPENSSL_SSL_RUNTIME_DLL}")
	endif()
	if (OPENSSL_CRYPTO_RUNTIME_DLL)
		list(APPEND MMO_WINDOWS_RUNTIME_DLLS "${OPENSSL_CRYPTO_RUNTIME_DLL}")
	endif()
endif()



# ===============================================================================
# MYSQL
# ===============================================================================

# Find MySQL
find_package(MYSQL REQUIRED)
include_directories(${MYSQL_INCLUDE_DIR})

if (WIN32 AND MYSQL_LIBRARY)
	get_filename_component(_mysql_library_dir "${MYSQL_LIBRARY}" DIRECTORY)
	find_file(MYSQL_RUNTIME_DLL
		NAMES libmysql.dll
		PATHS
			"${_mysql_library_dir}"
			"${_mysql_library_dir}/../bin"
		NO_DEFAULT_PATH
	)

	if (MYSQL_RUNTIME_DLL)
		list(APPEND MMO_WINDOWS_RUNTIME_DLLS "${MYSQL_RUNTIME_DLL}")
	endif()
endif()


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
# ASSIMP SDK
# ===============================================================================

if (MMO_BUILD_EDITOR)
	find_package(ASSIMP)
	if (WIN32 AND ASSIMPSDK_FOUND)
		file(GLOB _assimp_all_dlls "${ASSIMP_ROOT}/bin/*.dll")
		set(_assimp_release_dll "")
		set(_assimp_debug_dll "")
		foreach(_dll IN LISTS _assimp_all_dlls)
			if(_dll MATCHES "mtd\\.dll$")
				set(_assimp_debug_dll "${_dll}")
			else()
				set(_assimp_release_dll "${_dll}")
			endif()
		endforeach()
		if (_assimp_release_dll AND _assimp_debug_dll)
			list(APPEND MMO_WINDOWS_RUNTIME_DLLS_GENEX "$<IF:$<CONFIG:Debug>,${_assimp_debug_dll},${_assimp_release_dll}>")
		elseif (_assimp_release_dll)
			list(APPEND MMO_WINDOWS_RUNTIME_DLLS "${_assimp_release_dll}")
		elseif (_assimp_debug_dll)
			list(APPEND MMO_WINDOWS_RUNTIME_DLLS "${_assimp_debug_dll}")
		endif()
	endif()
endif()



# ===============================================================================
# IMGUI
# ===============================================================================

# If we want to build the editor, we need imgui for now
if (MMO_BUILD_EDITOR)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui/examples)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui-node-editor)
	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/imgui-node-editor/examples)
endif()


# ===============================================================================
# GLFW
# ===============================================================================

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw/include)


# METAL
if (APPLE)
    if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
        include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/metal-cpp)
        include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/metal-cpp-extensions)
    endif()
endif()

if (WIN32)
	list(REMOVE_DUPLICATES MMO_WINDOWS_RUNTIME_DLLS)
	message(STATUS "Windows runtime DLLs to deploy: ${MMO_WINDOWS_RUNTIME_DLLS}")
endif()
