# Copyright (C) 2019, Kyoril. All rights reserved.

file(GLOB hdrs "*.h")

add_library(network_hdrs INTERFACE)
target_sources(network_hdrs INTERFACE ${hdrs})
target_include_directories(network_hdrs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(network_hdrs INTERFACE base)

add_custom_target(network SOURCES ${hdrs})
source_group(src FILES ${hdrs})
set_property(TARGET network PROPERTY FOLDER "shared")
