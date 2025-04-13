# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

IF(APPLE)
    INCLUDE_DIRECTORIES ( /System/Library/Frameworks )
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    FIND_LIBRARY(METAL_LIBRARY Metal)
    MARK_AS_ADVANCED (COCOA_LIBRARY)
    SET(EXTRA_LIBS ${COCOA_LIBRARY} ${METAL_LIBRARY})
    
    # these are the OS X Interface Builder Files
    SET (Client_XIBS
        MainMenu
    )
ENDIF(APPLE)

# Add executable
file(GLOB srcFiles "*.cpp" "*.h")

if (APPLE)
	file(GLOB osxSources "macos/*.mm" "macos/*.h" "macos/*.m" "macos/*.cpp")
	source_group(src\\macos FILES ${osxSources})
endif()

if (WIN32)
	file(GLOB winSources "win/*.cpp" "win/*.h")
	source_group(src\\win FILES ${winSources})
endif()

source_group(src FILES ${srcFiles})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(graphics_test WIN32 MACOSX_BUNDLE ${srcFiles} ${osxSources} ${winSources})

IF(APPLE)
    SET_TARGET_PROPERTIES(graphics_test PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info-CMake.plist")
    SET(MACOSX_BUNDLE_INFO_STRING "")
    SET(MACOSX_BUNDLE_ICON_FILE "")
    SET(MACOSX_BUNDLE_GUI_IDENTIFIER "net.mmo-dev.${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_VERSION "0")
    SET(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2019 - 2025, Kyoril. All rights reserved.")
    SET(MACOSX_BUNDLE_NSMAIN_NIB_FILE "MainMenu")
    SET(MACOSX_BUNDLE_NSPRINCIPAL_CLASS "NSApplication")
ENDIF(APPLE)

# Link internal libraries
target_link_libraries(graphics_test 
	base 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	math 
	virtual_dir 
	tex_v1_0 
	assets
	scene_graph
    frame_ui
    terrain
    deferred_shading)
	
target_link_libraries(graphics_test graphics_null)
if (WIN32)
	target_link_libraries(graphics_test graphics_d3d11)
endif()

if(APPLE)
    target_link_libraries(graphics_test graphics_metal)
endif()

IF(APPLE)
    # Make sure we can find the 'ibtool' program. If we can NOT find it we
    # skip generation of this project
    FIND_PROGRAM(IBTOOL ibtool HINTS "/usr/bin" "${OSX_DEVELOPER_ROOT}/usr/bin")
    IF(${IBTOOL} STREQUAL "IBTOOL-NOTFOUND")
        MESSAGE(SEND_ERROR "ibtool can not be found and is needed to compile the .xib files. It should have been installed with the Apple developer tools. The default system paths were searched in addition to ${OSX_DEVELOPER_ROOT}/usr/bin")
    ENDIF()

    # Make sure the 'Resources' Directory is correctly created before we build
    ADD_CUSTOM_COMMAND (TARGET graphics_test PRE_BUILD COMMAND mkdir -p ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/graphics_test.app/Contents/Resources)
    ADD_CUSTOM_COMMAND (TARGET graphics_test PRE_BUILD COMMAND mkdir -p ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/graphics_test.app/Contents/Resources)

    # Compile the .xib files using the 'ibtool' program with the destination being the app package
    FOREACH(xib ${Client_XIBS})
        MESSAGE(STATUS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/graphics_test.app/Contents/Resources/${xib}.nib")
        MESSAGE(STATUS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/graphics_test.app/Contents/Resources/${xib}.nib")

        ADD_CUSTOM_COMMAND (TARGET graphics_test POST_BUILD
            COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text --compile ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/graphics_test.app/Contents/Resources/${xib}.nib ${CMAKE_CURRENT_SOURCE_DIR}/macos/${xib}.xib
            COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/macos/${xib}.xib")
        ADD_CUSTOM_COMMAND (TARGET graphics_test POST_BUILD
            COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text --compile ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/graphics_test.app/Contents/Resources/${xib}.nib ${CMAKE_CURRENT_SOURCE_DIR}/macos/${xib}.xib
            COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/macos/${xib}.xib")
    ENDFOREACH()
ENDIF(APPLE)

target_link_libraries(graphics_test ${EXTRA_LIBS})

# Add project to a solution folder for better organization
set_property(TARGET graphics_test PROPERTY FOLDER "client")

# Client is dpi aware
set_property(TARGET graphics_test PROPERTY VS_DPI_AWARE "PerMonitor")
