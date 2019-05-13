add_exe(hpak_tool)
target_link_libraries(hpak_tool hpak_v1_0 log base)
target_link_libraries(hpak_tool ${OPENSSL_LIBRARIES})
set_property(TARGET hpak_tool PROPERTY FOLDER "tools")
