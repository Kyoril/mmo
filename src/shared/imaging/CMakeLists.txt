
include_directories("${CMAKE_BINARY_DIR}/deps/libpng")

# Add shared imaging library
add_lib(imaging)
target_link_libraries(imaging assets)
target_link_libraries(imaging png_static)

# Put the library into the shared solution folder
set_property(TARGET imaging PROPERTY FOLDER "shared")
