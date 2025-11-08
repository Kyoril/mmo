project(null_audio)

# Null audio implementation (no-op audio for platforms without FMOD or for testing)
set(null_audio_SRC
	null_audio.h
	null_audio.cpp
)

add_library(null_audio ${null_audio_SRC})
target_include_directories(null_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(null_audio PUBLIC audio base math)
