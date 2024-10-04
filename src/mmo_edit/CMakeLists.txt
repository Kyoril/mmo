# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

IF(APPLE)
    INCLUDE_DIRECTORIES ( /System/Library/Frameworks )
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    MARK_AS_ADVANCED (COCOA_LIBRARY)
    SET(EXTRA_LIBS ${COCOA_LIBRARY})
    
    # these are the OS X Interface Builder Files
    SET (Client_XIBS
        MainMenu
    )
ENDIF(APPLE)

# Imgui sources
file(GLOB imgui_src 
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_draw.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_tables.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/imgui_widgets.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/misc/cpp/imgui_stdlib.h"
	"${PROJECT_SOURCE_DIR}/deps/imgui/misc/cpp/imgui_stdlib.cpp"

	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/crude_json.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_canvas.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor_api.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_node_editor_internal.inl"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_extra_math.inl"
	"${PROJECT_SOURCE_DIR}/deps/imgui-node-editor/imgui_bezier_math.inl"
)

if (WIN32)
file(GLOB imgui_platform_src
	"${PROJECT_SOURCE_DIR}/deps/imgui/backends/imgui_impl_win32.cpp"
	"${PROJECT_SOURCE_DIR}/deps/imgui/backends/imgui_impl_dx11.cpp"
)
else()
file(GLOB imgui_platform_src
	"${PROJECT_SOURCE_DIR}/deps/imgui/backends/imgui_impl_glfw.h"
	"${PROJECT_SOURCE_DIR}/deps/imgui/backends/imgui_impl_glfw.cpp"
)
endif()

file(GLOB editor_windows_src "editor_windows/*.h" "editor_windows/*.cpp")
file(GLOB preview_providers_src "preview_providers/*.h" "preview_providers/*.cpp")
file(GLOB editors_src "editors/*.h" "editors/*.cpp")
file(GLOB mat_editor_src "editors/material_editor/*.h" "editors/material_editor/*.cpp")
file(GLOB model_editor_src "editors/model_editor/*.h" "editors/model_editor/*.cpp")
file(GLOB tex_editor_src "editors/texture_editor/*.h" "editors/texture_editor/*.cpp")
file(GLOB world_editor_src "editors/world_editor/*.h" "editors/world_editor/*.cpp")
file(GLOB import_src "import/*.h" "import/*.cpp")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${ASSIMPSDK_INCLUDE_DIR})

source_group(src\\imgui FILES ${imgui_src})
source_group(src\\imgui\\platform FILES ${imgui_platform_src})
source_group(src\\editor_windows FILES ${editor_windows_src})
source_group(src\\preview_providers FILES ${preview_providers_src})
source_group(src\\editors FILES ${editors_src})
source_group(src\\editors\\material_editor FILES ${mat_editor_src})
source_group(src\\editors\\model_editor FILES ${model_editor_src})
source_group(src\\editors\\texture_editor FILES ${tex_editor_src})
source_group(src\\editors\\world_editor FILES ${world_editor_src})
source_group(src\\import FILES ${import_src})

# Add executable
add_gui_exe(mmo_edit ${imgui_src} ${imgui_platform_src} ${editor_windows_src} ${preview_providers_src} ${editors_src} ${mat_editor_src} ${model_editor_src} ${tex_editor_src} ${import_src} ${world_editor_src})

IF(APPLE)
    SET_TARGET_PROPERTIES(mmo_edit PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info-CMake.plist")
    SET(MACOSX_BUNDLE_INFO_STRING "")
    SET(MACOSX_BUNDLE_ICON_FILE "")
    SET(MACOSX_BUNDLE_GUI_IDENTIFIER "net.mmo-dev.${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_VERSION "0")
    SET(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2019 - 2022, Kyoril. All rights reserved.")
    SET(MACOSX_BUNDLE_NSMAIN_NIB_FILE "MainMenu")
    SET(MACOSX_BUNDLE_NSPRINCIPAL_CLASS "NSApplication")
ENDIF(APPLE)

target_link_libraries(mmo_edit 
	base 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	auth_protocol 
	math 
	virtual_dir 
	hpak_v1_0 
	tex_v1_0 
	assets
	scene_graph
	frame_ui
	paging
	proto_data
	libprotobuf
	sql_wrapper
	mysql_wrapper
	terrain)
	
target_link_libraries(mmo_edit graphics_null)
if (WIN32)
	target_link_libraries(mmo_edit graphics_d3d11)
endif()

target_link_libraries(mmo_edit ${ASSIMPSDK_LIBRARIES})
target_link_libraries(mmo_edit ${OPENSSL_LIBRARIES})
target_link_libraries(mmo_edit ${EXTRA_LIBS})
set_property(TARGET mmo_edit PROPERTY FOLDER "editor")

# Editor is dpi aware
set_property(TARGET mmo_edit PROPERTY VS_DPI_AWARE "PerMonitor")
