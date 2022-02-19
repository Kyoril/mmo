# Helper function for finding the FBX SDK.
# params: FBXSDK_VERSION
#         FBXSDK_SDKS
#
# sets:   FBXSDK_FOUND
#         FBXSDK_DIR
#         FBXSDK_LIBRARY
#         FBXSDK_LIBRARY_DEBUG
#         FBXSDK_LIBRARIES
#         FBXSDK_INCLUDE_DIR
#

# semi-hack to detect architecture
if( CMAKE_SIZEOF_VOID_P MATCHES 8 )
  # void ptr = 8 byte --> x86_64
  set(ARCH_32 OFF)
else()
  # void ptr != 8 byte --> x86
  set(ARCH_32 OFF)
endif()

if (NOT DEFINED FBXSDK_VERSION)
  set(FBXSDK_VERSION "2020.2")
endif()

set(_fbxsdk_vstudio_version "vs2019")
message(STATUS "Looking for FBX SDK version: ${FBXSDK_VERSION}")

if (NOT DEFINED FBXSDK_SDKS)
   set(FBXSDK_SDKS "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx")
endif()

get_filename_component(FBXSDK_SDKS_ABS ${FBXSDK_SDKS} ABSOLUTE)
set(FBXSDK_APPLE_ROOT   "${FBXSDK_SDKS_ABS}/Darwin/${FBXSDK_VERSION}")
set(FBXSDK_LINUX_ROOT   "${FBXSDK_SDKS_ABS}/Linux/${FBXSDK_VERSION}")
set(FBXSDK_WINDOWS_ROOT "${FBXSDK_SDKS_ABS}/Windows/${FBXSDK_VERSION}")

if (APPLE)
	set(_fbxsdk_download_file "Darwin")
	set(_fbxsdk_download_sha1 "a993d3bb5de25f0391b0957a6ded59222c6d38a1")
	set(_fbxsdk_root "${FBXSDK_APPLE_ROOT}")
	set(_fbxsdk_libdir_debug "lib/clang/debug")
	set(_fbxsdk_libdir_release "lib/clang/release")
	set(_fbxsdk_libname_debug "libfbxsdk.a")
	set(_fbxsdk_libname_release "libfbxsdk.a")
elseif (WIN32)
	set(_fbxsdk_download_file "Windows")
	set(_fbxsdk_download_sha1 "bffd44e96935513253a3065b00617c87c9f4ba76")
	set(_fbxsdk_root "${FBXSDK_WINDOWS_ROOT}")
	if (ARCH_32)
		set(_fbxsdk_libdir_debug "lib/${_fbxsdk_vstudio_version}/x86/debug")
		set(_fbxsdk_libdir_release "lib/${_fbxsdk_vstudio_version}/x86/release")
	else()
		set(_fbxsdk_libdir_debug "lib/${_fbxsdk_vstudio_version}/x64/debug")
		set(_fbxsdk_libdir_release "lib/${_fbxsdk_vstudio_version}/x64/release")
	endif()
	set(_fbxsdk_libname_debug "libfbxsdk-md.lib")
	set(_fbxsdk_libname_release "libfbxsdk-md.lib")
	
	set(_fbxsdk_libxmlname_debug "libxml2-md.lib")
	set(_fbxsdk_libxmlname_release "libxml2-md.lib")
	set(_fbxsdk_libzname_debug "zlib-md.lib")
	set(_fbxsdk_libzname_release "zlib-md.lib")
elseif (UNIX)
	set(_fbxsdk_download_file "Linux")
	set(_fbxsdk_download_sha1 "e1539f2cd5596918f9d7731fab0a92ed5d98b7d0")
	set(_fbxsdk_root "${FBXSDK_LINUX_ROOT}")
	if (ARCH_32)
		set(_fbxsdk_libdir_debug "lib/gcc/x86/debug")
		set(_fbxsdk_libdir_release "lib/gcc/x86/release")
	else()
		set(_fbxsdk_libdir_debug "lib/gcc/x64/debug")
		set(_fbxsdk_libdir_release "lib/gcc/x64/release")
	endif()
	set(_fbxsdk_libname_debug "libfbxsdk.a")
	set(_fbxsdk_libname_release "libfbxsdk.a")
else()
	message(FATAL_ERROR, "Unknown platform. Can't find FBX SDK.")
endif()

# Download file's if not existing

# First, check if the folder exists
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx/${_fbxsdk_download_file}")
	# Doesn't exist, check if the zip file exists
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx/${_fbxsdk_download_file}.tar.gz")
		# Doesn't exist as well, download zip file
		message(STATUS "Downloading fbx sdk...")
		file(DOWNLOAD "https://mmo-dev.net/${_fbxsdk_download_file}.tar.gz" "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx/${_fbxsdk_download_file}.tar.gz"
			   EXPECTED_HASH SHA1=${_fbxsdk_download_sha1}
			   STATUS status)
		
		# Obtain status code and string
		list(GET status 0 status_code)
		list(GET status 1 status_string)
		
		# Check for unsucessful download
		if(NOT status_code EQUAL 0)
			message(FATAL_ERROR "Downloading failed with status code ${status_code}: ${status_string}")
		endif()
	endif()
	
	# Extract zip file
	message(STATUS "Extracting fbx sdk...")
	
	# this is OS-agnostic
	execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx/${_fbxsdk_download_file}.tar.gz" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx RESULT_VARIABLE rv)
	
	# Log error if there is any
	if(NOT rv EQUAL 0)
		file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx/${_fbxsdk_download_file}")
		message(FATAL_ERROR "Error: fbx sdk extraction failed with error code ${rv}")
	else()
		# Cleanup archive file
		file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/deps/fbx/${_fbxsdk_download_file}.tar.gz")
	endif()
endif()

# should point the the FBX SDK installation dir
set(FBXSDK_ROOT "${_fbxsdk_root}")

# find header dir and libs
find_path(FBXSDK_INCLUDE_DIR "fbxsdk.h"
  NO_CMAKE_FIND_ROOT_PATH
  PATHS ${FBXSDK_ROOT}
  PATH_SUFFIXES "include")

find_library(FBXSDK_LIBRARY ${_fbxsdk_libname_release}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_release}")
  
find_library(FBXSDK_LIBXML_LIBRARY ${_fbxsdk_libxmlname_release}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_release}")
	
find_library(FBXSDK_ZLIB_LIBRARY ${_fbxsdk_libzname_release}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_release}")

find_library(FBXSDK_LIBRARY_DEBUG ${_fbxsdk_libname_debug}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_debug}")

find_library(FBXSDK_LIBXML_DEBUG ${_fbxsdk_libxmlname_debug}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_debug}")
  
find_library(FBXSDK_ZLIB_DEBUG ${_fbxsdk_libzname_debug}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${FBXSDK_ROOT}/${_fbxsdk_libdir_debug}")
  
set(FBXSDK_LIBRARIES 
	debug ${FBXSDK_LIBRARY_DEBUG} optimized ${FBXSDK_LIBRARY} 
	debug ${FBXSDK_LIBXML_DEBUG} optimized ${FBXSDK_LIBXML_LIBRARY}
	debug ${FBXSDK_ZLIB_DEBUG} optimized ${FBXSDK_ZLIB_LIBRARY})

if (FBXSDK_INCLUDE_DIR AND FBXSDK_LIBRARY AND FBXSDK_LIBRARY_DEBUG)
  set(FBXSDK_FOUND YES)
else()
  set(FBXSDK_FOUND NO)
endif()

