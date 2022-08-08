
# Add shared graphics library
add_lib(graphics_metal)
target_link_libraries(graphics_metal graphics)

# Put the library into the shared solution folder
set_property(TARGET graphics_metal PROPERTY FOLDER "shared")
