
# Procedural generation (editor-side, headless: no graphics dependency)
add_lib(pcg)
target_link_libraries(pcg base log math binary_io_hdrs)

set_property(TARGET pcg PROPERTY FOLDER "shared")
