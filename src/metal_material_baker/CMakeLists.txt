# macOS tool: adds Metal shaders to material files by recompiling their material graphs (see main.cpp).
add_executable(metal_material_baker main.cpp)

target_include_directories(metal_material_baker PRIVATE
	${PROJECT_SOURCE_DIR}/src/shared/binary_io)

target_link_libraries(metal_material_baker material_graph msl_cross scene_graph graphics_metal graphics graphics_null frame_ui tex_v1_0 tex assets virtual_dir hpak_v1_0 math base log)
target_link_libraries(metal_material_baker ${OPENSSL_LIBRARIES})
set_property(TARGET metal_material_baker PROPERTY FOLDER "tools")
