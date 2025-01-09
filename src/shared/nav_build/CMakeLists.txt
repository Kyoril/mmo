
# Create a new library
add_lib(nav_build)
target_link_libraries(nav_build base log math assets binary_io_hdrs terrain Recast Detour)

set_property(TARGET nav_build PROPERTY FOLDER "shared")
