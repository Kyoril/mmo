
file(GLOB hdrs "*.h")

add_library(binary_io_hdrs INTERFACE)
target_sources(binary_io_hdrs INTERFACE ${hdrs})
target_include_directories(binary_io_hdrs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

source_group(src FILES ${hdrs})
add_custom_target(binary_io SOURCES ${hdrs})
set_property(TARGET binary_io PROPERTY FOLDER "shared")
