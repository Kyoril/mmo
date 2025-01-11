
# Create a new library
add_lib(nav_build)
target_link_libraries(nav_build base log math assets binary_io_hdrs terrain Recast Detour scene_graph terrain graphics graphics_null tex tex_v1_0 frame_ui)

if (WIN32)
	target_link_libraries(nav_build graphics_d3d11)
endif()

set_property(TARGET nav_build PROPERTY FOLDER "shared")
