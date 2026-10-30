# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

IF(APPLE)
    INCLUDE_DIRECTORIES ( /System/Library/Frameworks )
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    FIND_LIBRARY(CARBON_LIBRARY Carbon)
    FIND_LIBRARY(METAL_LIBRARY Metal)
    MARK_AS_ADVANCED (COCOA_LIBRARY CARBON_LIBRARY)
    SET(EXTRA_LIBS ${COCOA_LIBRARY} ${CARBON_LIBRARY} ${METAL_LIBRARY})
ENDIF(APPLE)

if (MMO_WITH_DISCORD_RPC)
    add_definitions("-DMMO_WITH_DISCORD_RPC")
endif()

# Add executable
file(GLOB srcFiles "*.cpp" "*.h")
file(GLOB consoleSrcFiles "console/*.cpp" "console/*.h")
file(GLOB netSrcFiles "net/*.cpp" "net/*.h")
file(GLOB uiSrcFiles "ui/*.cpp" "ui/*.h")
file(GLOB sysSrcFiles "systems/*.cpp" "systems/*.h")
file(GLOB gsSrcFiles "game_states/*.cpp" "game_states/*.h")

if (WIN32)
	file(GLOB winSources "win/*.cpp" "win/*.h" "*.rc")
	source_group(src\\win FILES ${winSources})
endif()

if (APPLE)
	file(GLOB osxSources "macos/*.mm" "macos/*.h" "macos/*.m" "macos/*.cpp")
	source_group(src\\macos FILES ${osxSources})
endif()

source_group(src FILES ${srcFiles})
source_group(src\\console FILES ${consoleSrcFiles})
source_group(src\\net FILES ${netSrcFiles})
source_group(src\\ui FILES ${uiSrcFiles})
source_group(src\\game_states FILES ${gsSrcFiles})
source_group(src\\systems FILES ${sysSrcFiles})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR})  # Include directory where version.h is generated

# Add executable
add_gui_exe_recurse(mmo_client ${imgui_src} ${imgui_platform_src})
target_sources(mmo_client PRIVATE ${sysSrcFiles})

IF(APPLE)
    SET_TARGET_PROPERTIES(mmo_client PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info-CMake.plist")
    SET(MACOSX_BUNDLE_INFO_STRING "")
    SET(MACOSX_BUNDLE_ICON_FILE "")
    SET(MACOSX_BUNDLE_GUI_IDENTIFIER "net.mmo-dev.${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_VERSION "0")
    SET(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2019 - 2022, Kyoril. All rights reserved.")
    SET(MACOSX_BUNDLE_NSPRINCIPAL_CLASS "NSApplication")
ENDIF(APPLE)

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
    game_client
	scene_graph
    lua
    luabind
    client_data
    paging
    terrain
    deferred_shading)

if (WIN32)
	target_link_libraries(mmo_client fmod_audio)
else()
	target_link_libraries(mmo_client null_audio)
endif()
	
target_link_libraries(mmo_client graphics_null)
if (WIN32)
	target_link_libraries(mmo_client graphics_d3d11)
endif()

if(APPLE)
    target_link_libraries(mmo_client graphics_metal)
endif()

IF(APPLE)
    # The Objective-C++ platform layer uses automatic reference counting.
    file(GLOB osxObjcSources "${CMAKE_CURRENT_SOURCE_DIR}/macos/*.mm")
    set_source_files_properties(${osxObjcSources} PROPERTIES COMPILE_FLAGS "-fobjc-arc")
ENDIF(APPLE)

# Link external dependency libraries
target_link_libraries(mmo_client lua)
target_link_libraries(mmo_client ${OPENSSL_LIBRARIES})
target_link_libraries(mmo_client ${EXTRA_LIBS})

if (MMO_WITH_DISCORD_RPC)
    target_link_libraries(mmo_client discord-rpc)
endif()

# Add project to a solution folder for better organization
set_property(TARGET mmo_client PROPERTY FOLDER "client")

# Client is dpi aware
set_property(TARGET mmo_client PROPERTY VS_DPI_AWARE "PerMonitor")
