add_exe(nav_builder)
target_link_libraries(nav_builder log base nav_build)
set_property(TARGET nav_builder PROPERTY FOLDER "tools")

if(APPLE)
    target_link_libraries(nav_builder graphics_metal)
    target_link_libraries(nav_builder "-framework CoreFoundation")
endif()
