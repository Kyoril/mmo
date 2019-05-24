add_lib(assets)
target_link_libraries(assets base hpak_v1_0)

# Put the library into the shared solution folder
set_property(TARGET assets PROPERTY FOLDER "shared")
