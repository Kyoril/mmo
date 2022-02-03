
# Add shared graphics library
add_lib(graphics_d3d11)
target_link_libraries(graphics_d3d11 graphics assets)

# On windows platforms, we want to compile with d3d11 support
if(WIN32)
	file(GLOB d3d11_sources "*.cpp")
	file(GLOB d3d11_headers "*.h" "d3d11/*.hpp")
	file(GLOB d3d11_vs_shaders "shaders/VS_*.hlsl" "shaders/*.hlsli")
	file(GLOB d3d11_ps_shaders "shaders/PS_*.hlsl")

	target_sources(graphics_d3d11 PRIVATE ${d3d11_sources} ${d3d11_headers} ${d3d11_vs_shaders} ${d3d11_ps_shaders})
	source_group(src\\FILES ${d3d11_sources} ${d3d11_headers})
	source_group(src\\shaders FILES ${d3d11_vs_shaders})
	source_group(src\\haders FILES ${d3d11_ps_shaders})
	
	set_source_files_properties( ${d3d11_vs_shaders} PROPERTIES VS_SHADER_TYPE Vertex VS_SHADER_MODEL 5.0 VS_SHADER_ENTRYPOINT main 
		VS_SHADER_OUTPUT_HEADER_FILE "${CMAKE_CURRENT_LIST_DIR}/shaders/%(Filename).h" VS_SHADER_OBJECT_FILE_NAME "$(IntDir)%(Filename).cso"
		VS_SHADER_VARIABLE_NAME "g_%(Filename)")
	set_source_files_properties( ${d3d11_ps_shaders} PROPERTIES VS_SHADER_TYPE Pixel VS_SHADER_MODEL 5.0 VS_SHADER_ENTRYPOINT main 
		VS_SHADER_OUTPUT_HEADER_FILE "${CMAKE_CURRENT_LIST_DIR}/shaders/%(Filename).h" VS_SHADER_OBJECT_FILE_NAME "$(IntDir)%(Filename).cso"
		VS_SHADER_VARIABLE_NAME "g_%(Filename)")
endif()

# Put the library into the shared solution folder
set_property(TARGET graphics_d3d11 PROPERTY FOLDER "shared")
