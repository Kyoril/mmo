
IF(APPLE)
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    FIND_LIBRARY(QUARTZCORE_LIBRARY QuartzCore)
    MARK_AS_ADVANCED (COCOA_LIBRARY QUARTZCORE_LIBRARY)
    SET(EXTRA_LIBS ${COCOA_LIBRARY} ${QUARTZCORE_LIBRARY})
ENDIF(APPLE)

# Source files are listed explicitly rather than globbed: a glob does not re-run
# when files are added, and the previous top-level-only glob would have silently
# missed anything in a subdirectory.
set(codeFiles
    resource.h
    resource_names.h

    # Portable graphics core
    color.h
    geometry.h
    bitmap.h
    bitmap.cpp
    canvas.h
    canvas.cpp
    stb_impl.cpp
    text_renderer.h
    text_renderer.cpp

    # Portable resource access
    resource_data.h

    # Portable UI
    platform_host.h
    theme.h
    widget.h
    launcher_content.h
    launcher_content.cpp
    launcher_content_service.h
    launcher_content_service.cpp
    launcher_layout.h
    launcher_model.h
    launcher_model.cpp
    launcher_view.h
    launcher_view.cpp
    update_worker.h
    update_worker.cpp

    # Offscreen page renderer behind --render-preview, and the debug asset check
    launcher_preview.h
    launcher_preview.cpp
)

IF(WIN32)
    LIST(APPEND codeFiles
        platform/win32/main_win32.cpp
        platform/win32/launcher_window.h
        platform/win32/launcher_window.cpp
        platform/win32/resource_data_win32.cpp
    )
ENDIF(WIN32)

# Everything embedded into the executable by launcher.rc on Windows, and copied into
# the bundle's Resources directory on macOS. Neither CMake nor MSBuild parses .rc
# includes, so without an explicit OBJECT_DEPENDS below, editing an asset would not
# relink and the stale copy would silently keep shipping.
set(launcherAssets
    ${CMAKE_CURRENT_SOURCE_DIR}/res/news_development.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/news_world.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/border_frame_refined.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/splash.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/panel_bottom.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/button_up.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/button_over.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/button_down.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/button_disabled.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/progress_track.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/progress_fill.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/icon_close.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/icon_minimize.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/border_frame.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/content_texture.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/hero_frame.png
    ${CMAKE_CURRENT_SOURCE_DIR}/res/font_display.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/res/font_body.ttf
)

IF(APPLE)
    LIST(APPEND codeFiles
        platform/osx/launcher_osx.h
        platform/osx/launcher_window.h
        platform/osx/launcher_window.mm
        platform/osx/main_osx.mm
        platform/osx/resource_data_osx.mm
    )
    set_source_files_properties(
        platform/osx/launcher_window.mm
        platform/osx/main_osx.mm
        platform/osx/resource_data_osx.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")

    # The assets ship as plain files in Launcher.app/Contents/Resources, where
    # resource_data_osx.mm looks them up through the table in resource_names.h.
    set_source_files_properties(${launcherAssets} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
ENDIF(APPLE)

IF(APPLE)
    add_executable(launcher MACOSX_BUNDLE ${codeFiles} ${launcherAssets})
    SET_TARGET_PROPERTIES(launcher PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-CMake.plist")
    SET(MACOSX_BUNDLE_GUI_IDENTIFIER "net.mmo-dev.launcher")
    SET(MACOSX_BUNDLE_BUNDLE_NAME "Launcher")
    SET(MACOSX_BUNDLE_SHORT_VERSION_STRING "${MMO_LAUNCHER_VERSION_MAJOR}.${MMO_LAUNCHER_VERSION_MINOR}.${MMO_LAUNCHER_VERSION_PATCH}")
    SET(MACOSX_BUNDLE_BUNDLE_VERSION "${MMO_LAUNCHER_VERSION_MAJOR}.${MMO_LAUNCHER_VERSION_MINOR}.${MMO_LAUNCHER_VERSION_PATCH}")
    SET(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2019 - 2025, Kyoril. All rights reserved.")
ELSE()
    add_executable(launcher WIN32 launcher.rc ${codeFiles})
    set_source_files_properties(launcher.rc PROPERTIES OBJECT_DEPENDS "${launcherAssets}")
ENDIF()
source_group("res" FILES ${launcherAssets})
mmo_deploy_runtime_dependencies(launcher)


# Sources in platform/ subdirectories include the launcher's own portable headers by
# plain name, so the launcher root has to be on the include path.
target_include_directories(launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(launcher updater base log)
target_link_libraries(launcher ${OPENSSL_LIBRARIES} ${EXTRA_LIBS})

# freetype rasterizes the launcher's text into its own software canvas. It
# exports its include directories as PUBLIC, so linking it is sufficient.
target_link_libraries(launcher freetype)

set_target_properties(launcher PROPERTIES OUTPUT_NAME Launcher)

set_property(TARGET launcher PROPERTY FOLDER "client")

if (WIN32)
    # Client is dpi aware. NOTE: this property only emits PerMonitor *v1*; the
    # launcher additionally opts in to PerMonitorV2 at runtime via
    # SetProcessDpiAwarenessContext, since CMake has no value for v2.
    set_property(TARGET launcher PROPERTY VS_DPI_AWARE "PerMonitor")
endif()
