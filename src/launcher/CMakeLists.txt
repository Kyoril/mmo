
IF(APPLE)
    INCLUDE_DIRECTORIES ( /System/Library/Frameworks )
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    MARK_AS_ADVANCED (COCOA_LIBRARY)
    SET(EXTRA_LIBS ${COCOA_LIBRARY})
ENDIF(APPLE)

file(GLOB codeFiles
    "*.h"
    "*.cpp"
    "*.m"
    "*.mm"
)

IF(APPLE)
    FILE(GLOB appleSource "./osx/*.h" "./osx/*.m" "./osx/*.mm" "./osx/*.cpp" "osx/Logo_OSX.png")
    LIST(APPEND codeFiles ${appleSource})

    # these are the OS X Interface Builder Files
    SET (Launcher_XIBS
        MainMenu
    )
ENDIF(APPLE)


IF(APPLE)
add_executable(launcher MACOSX_BUNDLE ${codeFiles})
ELSE()
add_executable(launcher WIN32 launcher.rc ${codeFiles})
ENDIF()

IF(APPLE)
    SET_SOURCE_FILES_PROPERTIES(
        osx/Logo_OSX.png
        PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
ENDIF(APPLE)


IF(APPLE)
    SET_TARGET_PROPERTIES(launcher PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-CMake.plist")
    SET(MACOSX_BUNDLE_INFO_STRING "")
    SET(MACOSX_BUNDLE_ICON_FILE "")
    SET(MACOSX_BUNDLE_GUI_IDENTIFIER "net.mmo-dev.${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_LONG_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_NAME "${CMAKE_PROJECT_NAME}")
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0")
    SET(MACOSX_BUNDLE_BUNDLE_VERSION "0")
    SET(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2019 - 2022, Robin Klimonow. All rights reserved.")
    SET(MACOSX_BUNDLE_NSMAIN_NIB_FILE "MainMenu")
    SET(MACOSX_BUNDLE_NSPRINCIPAL_CLASS "NSApplication")
ENDIF(APPLE)

IF(APPLE)
    # Make sure we can find the 'ibtool' program. If we can NOT find it we
    # skip generation of this project
    FIND_PROGRAM(IBTOOL ibtool HINTS "/usr/bin" "${OSX_DEVELOPER_ROOT}/usr/bin")
    IF(${IBTOOL} STREQUAL "IBTOOL-NOTFOUND")
        MESSAGE(SEND_ERROR "ibtool can not be found and is needed to compile the .xib files. It should have been installed with the Apple developer tools. The default system paths were searched in addition to ${OSX_DEVELOPER_ROOT}/usr/bin")
    ENDIF()

    # Make sure the 'Resources' Directory is correctly created before we build
    ADD_CUSTOM_COMMAND (TARGET launcher PRE_BUILD COMMAND mkdir -p ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/Launcher.app/Contents/Resources)
    ADD_CUSTOM_COMMAND (TARGET launcher PRE_BUILD COMMAND mkdir -p ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/Launcher.app/Contents/Resources)

    # Compile the .xib files using the 'ibtool' program with the destination being the app package
    FOREACH(xib ${Launcher_XIBS})
        MESSAGE(STATUS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/Launcher.app/Contents/Resources/${xib}.nib")
        MESSAGE(STATUS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/Launcher.app/Contents/Resources/${xib}.nib")

        ADD_CUSTOM_COMMAND (TARGET launcher POST_BUILD
            COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text --compile ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Debug/Launcher.app/Contents/Resources/${xib}.nib ${CMAKE_CURRENT_SOURCE_DIR}/${xib}.xib
            COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/${xib}.xib")
        ADD_CUSTOM_COMMAND (TARGET launcher POST_BUILD
            COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text --compile ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Release/Launcher.app/Contents/Resources/${xib}.nib ${CMAKE_CURRENT_SOURCE_DIR}/${xib}.xib
            COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/${xib}.xib")
    ENDFOREACH()
ENDIF(APPLE)


target_link_libraries(launcher updater base log)
target_link_libraries(launcher ${OPENSSL_LIBRARIES} ${EXTRA_LIBS})
set_target_properties(launcher PROPERTIES OUTPUT_NAME Launcher)

set_property(TARGET launcher PROPERTY FOLDER "client")

if (WIN32)
    # Client is dpi aware
    set_property(TARGET launcher PROPERTY VS_DPI_AWARE "PerMonitor")
endif()
