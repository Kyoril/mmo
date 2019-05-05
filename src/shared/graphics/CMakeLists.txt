
# Add shared graphics library
add_lib(graphics)

# On windows platforms, we want to compile with d3d11 support
if(WIN32)
	file(GLOB d3d11_sources "d3d11/*.cpp")
	file(GLOB d3d11_headers "d3d11/*.h" "d3d11/*.hpp")

	target_sources(graphics PRIVATE ${d3d11_sources} ${d3d11_headers})
	#target_sources(graphics PUBLIC )
	source_group(src\\d3d11 FILES ${d3d11_sources} ${d3d11_headers})
endif()

# Put the library into the shared solution folder
set_property(TARGET graphics PROPERTY FOLDER "shared")
