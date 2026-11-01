# The material graph: nodes, pins, links and their (de)serialization, compiling down to shader code through
# the material compiler. It has no UI dependency, so the editor, the development client and the cooking tool
# all share it. Node layout is owned by whoever displays the graph, see INodeLayoutStore.
add_lib(material_graph)
target_link_libraries(material_graph graphics assets binary_io_hdrs math base log)

# Put the library into the shared solution folder
set_property(TARGET material_graph PROPERTY FOLDER "shared")
