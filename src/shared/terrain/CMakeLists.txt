
# Create a new library
add_lib(terrain)
target_link_libraries(terrain graphics)

set_property(TARGET terrain PROPERTY FOLDER "shared")
