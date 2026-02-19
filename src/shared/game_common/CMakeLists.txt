
# Shared library for common game code used by both client and editor
# This library has no protobuf dependencies to avoid symbol conflicts
add_lib(game_common)
target_link_libraries(game_common base math scene_graph)

# Put the library into the shared solution folder
set_property(TARGET game_common PROPERTY FOLDER "shared")
