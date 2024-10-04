add_lib(updater)
target_link_libraries(updater virtual_dir http_client https_client hpak_v1_0 base)
set_property(TARGET updater PROPERTY FOLDER "shared")
