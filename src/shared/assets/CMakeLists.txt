add_lib(assets)
target_link_libraries(assets base log hpak_v1_0 virtual_dir)

# Put the library into the shared solution folder
set_property(TARGET assets PROPERTY FOLDER "shared")
