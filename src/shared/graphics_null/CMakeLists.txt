
# Add shared graphics library
add_lib(graphics_null)
target_link_libraries(graphics_null graphics)

# Put the library into the shared solution folder
set_property(TARGET graphics_null PROPERTY FOLDER "shared")
