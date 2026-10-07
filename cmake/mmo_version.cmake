# This file extracts the git commit count and other infos and fills them
# in templated header files which are used to show version informations in the
# compiled applications.
#
# Version rules (tools/release/version.py applies the same ones, see docs/versioning.md):
#   game     = MAJOR.MINOR.PATCH of the newest reachable vX.Y.Z tag, plus the commit count
#   launcher = MAJOR.MINOR.PATCH of the newest reachable launcher-vX.Y.Z tag
# A shallow clone has no tags and a commit count of 1, so release builds need full history.

# Defaults in case git is not available (e.g. Docker builds without .git)
set(MMO_GIT_COMMIT "UNKNOWN")
set(MMO_GIT_REVISION 0)
set(MMO_GIT_LASTCHANGE "UNKNOWN")
set(MMO_GIT_BRANCH "UNKNOWN")

# Sets <out>_MAJOR/_MINOR/_PATCH from the newest tag matching <prefix>X.Y.Z, or 0.0.0.
function(mmo_version_from_tag prefix out)
	set(major 0)
	set(minor 0)
	set(patch 0)
	if(GIT_FOUND)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 --match "${prefix}[0-9]*" HEAD
			WORKING_DIRECTORY "${MMO_VERSION_SOURCE_DIR}"
			OUTPUT_VARIABLE tag
			RESULT_VARIABLE result
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		string(LENGTH "${prefix}" prefixLength)
		if(result EQUAL 0)
			string(SUBSTRING "${tag}" ${prefixLength} -1 numbers)
			if(numbers MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
				set(major ${CMAKE_MATCH_1})
				set(minor ${CMAKE_MATCH_2})
				set(patch ${CMAKE_MATCH_3})
			endif()
		endif()
	endif()
	set(${out}_MAJOR ${major} PARENT_SCOPE)
	set(${out}_MINOR ${minor} PARENT_SCOPE)
	set(${out}_PATCH ${patch} PARENT_SCOPE)
endfunction()

if(NOT MMO_VERSION_SOURCE_DIR)
	set(MMO_VERSION_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Version header
if(EXISTS ${MMO_VERSION_SOURCE_DIR}/.git)
	find_package(Git)
	if(GIT_FOUND)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
			WORKING_DIRECTORY "${MMO_VERSION_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_COMMIT"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count
			WORKING_DIRECTORY "${MMO_VERSION_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_REVISION"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} show -s --format=%ci
			WORKING_DIRECTORY "${MMO_VERSION_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_LASTCHANGE"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
			WORKING_DIRECTORY "${MMO_VERSION_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_BRANCH"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		# Fall back to safe defaults if git commands returned empty results
		# (e.g. when building inside Docker where the .git history is unavailable)
		if(NOT MMO_GIT_COMMIT)
			set(MMO_GIT_COMMIT "UNKNOWN")
		endif()
		if(NOT MMO_GIT_REVISION)
			set(MMO_GIT_REVISION 0)
		endif()
		if(NOT MMO_GIT_LASTCHANGE)
			set(MMO_GIT_LASTCHANGE "UNKNOWN")
		endif()
		if(NOT MMO_GIT_BRANCH)
			set(MMO_GIT_BRANCH "UNKNOWN")
		endif()
	endif(GIT_FOUND)
endif()

mmo_version_from_tag("v" MMO_VERSION)
mmo_version_from_tag("launcher-v" MMO_LAUNCHER_VERSION)

# Setup config files (skipped by tools/tests/test_release_version.py, which runs this in script mode)
if(NOT MMO_VERSION_PRINT_ONLY)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${PROJECT_BINARY_DIR}/version.h @ONLY)
	#configure_file(${CMAKE_CURRENT_SOURCE_DIR}/editor_config.h.in ${PROJECT_BINARY_DIR}/editor_config.h @ONLY)
else()
	message("${MMO_VERSION_MAJOR}.${MMO_VERSION_MINOR}.${MMO_VERSION_PATCH}.${MMO_GIT_REVISION} ${MMO_LAUNCHER_VERSION_MAJOR}.${MMO_LAUNCHER_VERSION_MINOR}.${MMO_LAUNCHER_VERSION_PATCH}")
endif()
