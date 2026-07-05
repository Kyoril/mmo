
# Create a new library
add_lib(terrain_io)
target_link_libraries(terrain_io base log math binary_io_hdrs)

set_property(TARGET terrain_io PROPERTY FOLDER "shared")
