# macOS tool: adds Metal shaders to material files by recompiling their material graphs (see main.cpp).
set(editorMaterialDir "${PROJECT_SOURCE_DIR}/src/mmo_edit/editors/material_editor")

# The graph nodes are editor code and reference ImGui and the node editor, though not for loading
# and compiling, which is all this tool does.
set(bakerImguiSources
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_draw.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_tables.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_widgets.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/crude_json.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_canvas.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor_api.cpp")
add_executable(metal_material_baker
	main.cpp
	${PROJECT_SOURCE_DIR}/src/shared/graphics_d3d11/material_compiler_d3d11.cpp
	${editorMaterialDir}/material_graph.cpp
	${editorMaterialDir}/material_node.cpp
	${editorMaterialDir}/material_function.cpp
	${editorMaterialDir}/material_function_manager.cpp
	${editorMaterialDir}/node_editor/node_registry.cpp
	${bakerImguiSources})

target_include_directories(metal_material_baker PRIVATE
	${PROJECT_SOURCE_DIR}/src/mmo_edit
	${PROJECT_SOURCE_DIR}/src/shared/binary_io
	${PROJECT_SOURCE_DIR}/deps/imgui
	${PROJECT_SOURCE_DIR}/deps/imgui-node-editor)

target_link_libraries(metal_material_baker msl_cross scene_graph graphics_metal graphics graphics_null frame_ui tex_v1_0 tex assets virtual_dir hpak_v1_0 math base log)
target_link_libraries(metal_material_baker ${OPENSSL_LIBRARIES})
set_property(TARGET metal_material_baker PROPERTY FOLDER "tools")
