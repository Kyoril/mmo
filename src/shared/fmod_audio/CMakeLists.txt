project(fmod_audio)

# FMOD audio implementation (Windows, and macOS when the FMOD SDK is available, see
# mmo_external_dependencies.cmake)
set(fmod_audio_SRC
	fmod_audio.h
	fmod_audio.cpp
)

add_library(fmod_audio ${fmod_audio_SRC})
target_include_directories(fmod_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(fmod_audio PUBLIC "${MMO_FMOD_INCLUDE_DIR}")
target_compile_definitions(fmod_audio PUBLIC MMO_WITH_FMOD)
target_link_libraries(fmod_audio PUBLIC audio base math)
target_link_libraries(fmod_audio PUBLIC "${MMO_FMOD_LIBRARY}")

if (WIN32)
	# Copy FMOD DLL to binary directory for any target that links this library
	add_custom_command(TARGET fmod_audio POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/$<CONFIG>"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${MMO_FMOD_RUNTIME_LIBRARY}"
		"${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/fmod.dll"
		COMMENT "Copying FMOD DLL for fmod_audio library"
	)
endif()
