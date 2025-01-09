add_exe(nav_builder)
target_link_libraries(nav_builder log base nav_build)
set_property(TARGET nav_builder PROPERTY FOLDER "tools")
