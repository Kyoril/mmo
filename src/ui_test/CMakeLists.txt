# UI test app: renders the frame UI outside the game client and reloads it when UI files change.
# See tools/ui_test/README.md. Windows only for now (it needs a native event loop per platform).

file(GLOB srcFiles "*.cpp" "*.h")
file(GLOB winSources "win/*.cpp" "win/*.h")
source_group(src FILES ${srcFiles})
source_group(src\\win FILES ${winSources})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ui_test WIN32 ${srcFiles} ${winSources})
mmo_deploy_runtime_dependencies(ui_test)

target_link_libraries(ui_test
	base
	log
	simple_file_format_hdrs
	binary_io_hdrs
	math
	virtual_dir
	tex_v1_0
	assets
	frame_ui
	expat
	lua
	luabind
	graphics_null
	graphics_d3d11)

set_property(TARGET ui_test PROPERTY FOLDER "client")
set_property(TARGET ui_test PROPERTY VS_DPI_AWARE "PerMonitor")
