add_lib(tex)
target_link_libraries(tex zlibstatic)
set_property(TARGET tex PROPERTY FOLDER "shared")
