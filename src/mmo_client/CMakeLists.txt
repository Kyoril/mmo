# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

# Add executable
file(GLOB srcFiles "*.cpp" "*.h")
file(GLOB consoleSrcFiles "console/*.cpp" "console/*.h")
file(GLOB netSrcFiles "net/*.cpp" "net/*.h")
file(GLOB uiSrcFiles "ui/*.cpp" "ui/*.h")
file(GLOB gsSrcFiles "game_states/*.cpp" "game_states/*.h")

source_group(src FILES ${srcFiles})
source_group(src\\console FILES ${consoleSrcFiles})
source_group(src\\net FILES ${netSrcFiles})
source_group(src\\ui FILES ${uiSrcFiles})
source_group(src\\game_states FILES ${gsSrcFiles})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(mmo_client WIN32 MACOSX_BUNDLE ${srcFiles} ${consoleSrcFiles} ${netSrcFiles} ${uiSrcFiles} ${gsSrcFiles})

# Link internal libraries
target_link_libraries(mmo_client 
	base 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	auth_protocol 
	game_protocol
	math 
	virtual_dir 
	hpak_v1_0 
	tex_v1_0 
	assets
	frame_ui
	expat
	game
	mesh
	mesh_v1_0
	scene_graph)
	
target_link_libraries(mmo_client graphics_null)
if (WIN32)
	target_link_libraries(mmo_client graphics_d3d11)
endif()

# Link external dependency libraries
target_link_libraries(mmo_client lua)
target_link_libraries(mmo_client ${OPENSSL_LIBRARIES})

# Add project to a solution folder for better organization
set_property(TARGET mmo_client PROPERTY FOLDER "client")

# Client is dpi aware
set_property(TARGET mmo_client PROPERTY VS_DPI_AWARE "PerMonitor")
