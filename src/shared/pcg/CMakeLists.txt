
# Procedural generation (editor-side, headless: no graphics dependency)
add_lib(pcg)
target_link_libraries(pcg base log math)

set_property(TARGET pcg PROPERTY FOLDER "shared")
