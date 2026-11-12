# Procedural generation (editor-side, headless: no graphics dependency)
add_lib(pcg)

# add_lib only globs the current folder; the graph core lives in graph/.
file(GLOB pcg_graph_sources CONFIGURE_DEPENDS "graph/*.cpp")
file(GLOB pcg_graph_headers CONFIGURE_DEPENDS "graph/*.h")
target_sources(pcg PRIVATE ${pcg_graph_sources} ${pcg_graph_headers})
source_group(graph FILES ${pcg_graph_sources} ${pcg_graph_headers})

target_link_libraries(pcg base log math binary_io_hdrs terrain_io)

set_property(TARGET pcg PROPERTY FOLDER "shared")
