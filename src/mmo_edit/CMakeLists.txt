# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

# Imgui sources
file(GLOB imgui_src 
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_draw.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_tables.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_widgets.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/misc/cpp/imgui_stdlib.h"
	"${PROJECT_SOURCE_DIR}/deps/imgui/misc/cpp/imgui_stdlib.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/backends/imgui_impl_win32.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/backends/imgui_impl_dx11.cpp"

	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/crude_json.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_canvas.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor_api.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor_internal.inl"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_extra_math.inl"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_bezier_math.inl"
	
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/examples/blueprints-example/utilities/builders.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/examples/blueprints-example/utilities/widgets.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/examples/blueprints-example/utilities/drawing.cpp"
)

file(GLOB editor_windows_src "editor_windows/*.h" "editor_windows/*.cpp")
file(GLOB preview_providers_src "preview_providers/*.h" "preview_providers/*.cpp")
file(GLOB editors_src "editors/*.h" "editors/*.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${FBXSDK_INCLUDE_DIR})

source_group(src\\imgui FILES ${imgui_src})
source_group(src\\editor_windows FILES ${editor_windows_src})
source_group(src\\preview_providers FILES ${preview_providers_src})
source_group(src\\editors FILES ${editors_src})

# Add executable
add_gui_exe(mmo_edit ${imgui_src} ${editor_windows_src} ${preview_providers_src} ${editors_src})

target_link_libraries(mmo_edit 
	base 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	auth_protocol 
	graphics 
	math 
	virtual_dir 
	hpak_v1_0 
	tex_v1_0 
	assets
	mesh
	mesh_v1_0
	imaging
	data
	libprotobuf
	scene_graph
	frame_ui)
	
target_link_libraries(mmo_edit ${FBXSDK_LIBRARIES})
target_link_libraries(mmo_edit ${OPENSSL_LIBRARIES})
set_property(TARGET mmo_edit PROPERTY FOLDER "editor")

# Editor is dpi aware
set_property(TARGET mmo_edit PROPERTY VS_DPI_AWARE "PerMonitor")
