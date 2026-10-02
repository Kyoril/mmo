add_exe(nav_query)
target_link_libraries(nav_query base log math assets binary_io_hdrs nav_mesh)
set_property(TARGET nav_query PROPERTY FOLDER "tools")
