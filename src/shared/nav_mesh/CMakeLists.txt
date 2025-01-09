
# Create a new library
add_lib(nav_mesh)
target_link_libraries(nav_mesh base math assets binary_io_hdrs Recast Detour)

set_property(TARGET nav_mesh PROPERTY FOLDER "shared")
