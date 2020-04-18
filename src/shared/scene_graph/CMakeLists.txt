
# Add shared graphics library
add_lib(scene_graph)
target_link_libraries(scene_graph base)

# Put the library into the shared solution folder
set_property(TARGET scene_graph PROPERTY FOLDER "shared")
