# This file extracts the git commit count and other infos and fills them 
# in templated header files which are used to show version informations in the 
# compiled applications.

# Defaults in case git is not available (e.g. Docker builds without .git)
set(MMO_GIT_COMMIT "UNKNOWN")
set(MMO_GIT_REVISION 0)
set(MMO_GIT_LASTCHANGE "UNKNOWN")
set(MMO_GIT_BRANCH "UNKNOWN")

# Version header
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
	find_package(Git)
	if(GIT_FOUND)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_COMMIT"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-list HEAD --count
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_REVISION"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} show -s --format=%ci
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
			OUTPUT_VARIABLE "MMO_GIT_LASTCHANGE"
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
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

# Setup config files
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${PROJECT_BINARY_DIR}/version.h @ONLY)
#configure_file(${CMAKE_CURRENT_SOURCE_DIR}/editor_config.h.in ${PROJECT_BINARY_DIR}/editor_config.h @ONLY)
