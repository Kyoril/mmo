
# Add shared imaging library
add_lib(imaging)
target_link_libraries(imaging assets)
target_link_libraries(imaging lodepng)

# Put the library into the shared solution folder
set_property(TARGET imaging PROPERTY FOLDER "shared")
