# Helper function for finding the ASSIMP SDK.
# sets:   ASSIMP_FOUND
#         ASSIMP_DIR
#         ASSIMP_LIBRARY
#         ASSIMP_LIBRARY_DEBUG
#         ASSIMP_LIBRARIES
#         ASSIMP_INCLUDE_DIR
#

# semi-hack to detect architecture
if( CMAKE_SIZEOF_VOID_P MATCHES 8 )
  # void ptr = 8 byte --> x86_64
  set(ARCH_32 OFF)
else()
  # void ptr != 8 byte --> x86
  set(ARCH_32 OFF)
endif()

set(_assimp_vstudio_version "VS2022")

if (NOT DEFINED ASSIMPSDK_SDKS)
   set(ASSIMPSDK_SDKS "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp")
endif()

get_filename_component(ASSIMPSDK_SDKS_ABS ${ASSIMPSDK_SDKS} ABSOLUTE)
set(ASSIMP_APPLE_ROOT   "${ASSIMPSDK_SDKS_ABS}/Darwin")
set(ASSIMP_WINDOWS_ROOT "${ASSIMPSDK_SDKS_ABS}/Windows/${_assimp_vstudio_version}")

if (APPLE)
	set(_assimpsdk_download_file "AssimpDarwin")
	set(_assimpsdk_download_sha1 "a993d3bb5de25f0391b0957a6ded59222c6d38a1")
	set(_assimpsdk_root "${ASSIMP_APPLE_ROOT}")
	set(_assimpsdk_libdir_debug "lib/clang/debug")
	set(_assimpsdk_libdir_release "lib/clang/release")
	set(_assimpsdk_libname_debug "libassimp.a")
	set(_assimpsdk_libname_release "libassimp.a")
elseif (WIN32)
	set(_assimpsdk_download_file "AssimpWindows")
	set(_assimpsdk_download_sha1 "094f0dd1fc993167d532665a70a381d45aedeb35")
	set(_assimpsdk_root "${ASSIMP_WINDOWS_ROOT}")
	if (ARCH_32)
		set(_assimpsdk_libdir_debug "lib/${_assimpsdk_vstudio_version}/x86")
		set(_assimpsdk_libdir_release "lib/${_assimpsdk_vstudio_version}/x86")
	else()
		set(_assimpsdk_libdir_debug "lib/${_assimpsdk_vstudio_version}/x64")
		set(_assimpsdk_libdir_release "lib/${_assimpsdk_vstudio_version}/x64")
	endif()
	set(_assimpsdk_libname_debug "assimpD.lib")
	set(_assimpsdk_libname_release "assimp.lib")
else()
	message(FATAL_ERROR, "Unknown platform. Can't find ASSIMP SDK.")
endif()

# Download file's if not existing

# First, check if the folder exists
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp/Windows")
	# Doesn't exist, check if the zip file exists
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp/Windows/${_assimp_vstudio_version}.tar.gz")
		# Doesn't exist as well, download zip file
		message(STATUS "Downloading assimp sdk...")
		file(DOWNLOAD "https://mmo-dev.net/${_assimpsdk_download_file}.tar.gz" "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp/${_assimpsdk_download_file}.tar.gz"
			   EXPECTED_HASH SHA1=${_assimpsdk_download_sha1}
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
	message(STATUS "Extracting assimp sdk...")
	
	# this is OS-agnostic
	execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp/${_assimpsdk_download_file}.tar.gz" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp RESULT_VARIABLE rv)
	
	# Log error if there is any
	if(NOT rv EQUAL 0)
		file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp/${_assimpsdk_download_file}")
		message(FATAL_ERROR "Error: assimp sdk extraction failed with error code ${rv}")
	else()
		# Cleanup archive file
		file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/deps/assimp/${_assimpsdk_download_file}.tar.gz")
	endif()
endif()

# should point the the ASSIMP SDK installation dir
set(ASSIMP_ROOT "${_assimpsdk_root}")

# find header dir and libs
find_path(ASSIMPSDK_INCLUDE_DIR "assimp/Importer.hpp"
  NO_CMAKE_FIND_ROOT_PATH
  PATHS ${ASSIMP_ROOT}
  PATH_SUFFIXES "include")

find_library(ASSIMPSDK_LIBRARY ${_assimpsdk_libname_release}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${ASSIMP_ROOT}/${_assimpsdk_libdir_release}")
  
find_library(ASSIMPSDK_LIBRARY_DEBUG ${_assimpsdk_libname_debug}
  NO_CMAKE_FIND_ROOT_PATH
  PATHS "${ASSIMP_ROOT}/${_assimpsdk_libdir_debug}")
  
set(ASSIMPSDK_LIBRARIES
    debug ${ASSIMPSDK_LIBRARY_DEBUG} 
	optimized ${ASSIMPSDK_LIBRARY}
	)

if (ASSIMPSDK_INCLUDE_DIR AND ASSIMPSDK_LIBRARY AND ASSIMPSDK_LIBRARY_DEBUG)
  set(ASSIMPSDK_FOUND YES)
else()
  set(ASSIMPSDK_FOUND NO)
  message(STATUS "ASSIMPSDK_INCLUDE_DIR: ${ASSIMPSDK_INCLUDE_DIR}")
  message(STATUS "ASSIMPSDK_LIBRARY: ${ASSIMPSDK_LIBRARY}")
  message(STATUS "ASSIMPSDK_LIBRARY_DEBUG: ${ASSIMPSDK_LIBRARY_DEBUG}")
endif()

