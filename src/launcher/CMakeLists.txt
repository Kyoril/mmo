
add_gui_exe(launcher)
target_link_libraries(launcher updater base log)
target_link_libraries(launcher ${OPENSSL_LIBRARIES})
set_target_properties(launcher PROPERTIES OUTPUT_NAME Launcher)

set_property(TARGET launcher PROPERTY FOLDER "client")

# Client is dpi aware
set_property(TARGET launcher PROPERTY VS_DPI_AWARE "PerMonitor")