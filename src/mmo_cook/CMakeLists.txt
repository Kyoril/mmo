# Release content cook: builds every distributed platform's material shaders and assembles them into
# a client overlay the deployer lays over the data checkout. See docs/superpowers/specs/2026-10-31-content-cooking-design.md.
add_exe(mmo_cook)
target_link_libraries(mmo_cook content_cook material_graph graphics graphics_null assets virtual_dir hpak_v1_0 tex tex_v1_0 binary_io_hdrs math base log ${OPENSSL_LIBRARIES})
if (WIN32)
	target_link_libraries(mmo_cook graphics_d3d11)
elseif (APPLE AND TARGET msl_cross)
	target_link_libraries(mmo_cook msl_cross graphics_metal "-framework CoreFoundation")
	target_compile_definitions(mmo_cook PRIVATE MMO_COOK_WITH_METAL)
endif()
set_property(TARGET mmo_cook PROPERTY FOLDER "tools")
