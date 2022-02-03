
# Add shared graphics library
add_lib(graphics)
target_link_libraries(graphics assets)

# Put the library into the shared solution folder
set_property(TARGET graphics PROPERTY FOLDER "shared")
