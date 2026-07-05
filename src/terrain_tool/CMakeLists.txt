add_exe(terrain_tool)
target_link_libraries(terrain_tool base log math binary_io_hdrs assets terrain_io zlibstatic)
set_property(TARGET terrain_tool PROPERTY FOLDER "tools")
