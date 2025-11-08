project(fmod_audio)

# FMOD audio implementation (Windows only)
if (WIN32)
	set(fmod_audio_SRC
		fmod_audio.h
		fmod_audio.cpp
	)

	add_library(fmod_audio ${fmod_audio_SRC})
	target_include_directories(fmod_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
	target_include_directories(fmod_audio PUBLIC "${CMAKE_SOURCE_DIR}/deps/fmod/inc")
	target_link_libraries(fmod_audio PUBLIC audio base math)
	target_link_libraries(fmod_audio PUBLIC "${CMAKE_SOURCE_DIR}/deps/fmod/lib/x64/fmod_vc.lib")
	
	# Copy FMOD DLL to binary directory for any target that links this library
	add_custom_command(TARGET fmod_audio POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_SOURCE_DIR}/deps/fmod/lib/x64/fmod.dll"
		"${CMAKE_SOURCE_DIR}/bin/$<CONFIG>/"
		COMMENT "Copying FMOD DLL for fmod_audio library"
	)
endif()
