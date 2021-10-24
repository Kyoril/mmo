add_lib(update_compilation)
target_link_libraries(update_compilation virtual_dir base zlibstatic)
set_property(TARGET update_compilation PROPERTY FOLDER "shared")
