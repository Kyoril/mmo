project(audio)

set(audio_SRC
	audio.h
)

add_library(audio INTERFACE)
target_sources(audio INTERFACE FILE_SET HEADERS FILES ${audio_SRC})
target_include_directories(audio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(audio INTERFACE base math)
