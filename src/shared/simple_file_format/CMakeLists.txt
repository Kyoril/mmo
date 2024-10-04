# Copyright (C) 2019, Kyoril. All rights reserved.

file(GLOB hdrs "*.h")

add_library(simple_file_format_hdrs INTERFACE)
target_sources(simple_file_format_hdrs INTERFACE ${hdrs})
target_include_directories(simple_file_format_hdrs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_target(simple_file_format SOURCES ${hdrs})
source_group(src FILES ${hdrs})
set_property(TARGET simple_file_format PROPERTY FOLDER "shared")
