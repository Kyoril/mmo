# Catch2 v2, vendored as a single header.
#
# catch2      - the header itself, exposed as an include directory.
# catch_main  - Catch2's default main(), compiled once and linked by every test target.
#
# Test targets never link catch2 directly; mmo_add_test() links catch_main, which carries
# catch2 as a PUBLIC dependency so the include directory propagates.

add_library(catch2 INTERFACE)
target_include_directories(catch2 INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(catch_main STATIC catch_main.cpp)
target_link_libraries(catch_main PUBLIC catch2)
set_property(TARGET catch_main PROPERTY FOLDER "deps")
