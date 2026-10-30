# Metal graphics backend (macOS). Written in Objective-C++ with automatic reference counting.
add_lib(graphics_metal)
target_compile_options(graphics_metal PRIVATE -fobjc-arc)
target_link_libraries(graphics_metal graphics graphics_null
	"-framework Metal" "-framework QuartzCore" "-framework AppKit" "-framework Foundation")

# The fixed vertex format shaders are the same HLSL the D3D11 backend compiles with fxc.
file(GLOB builtin_shaders "${CMAKE_CURRENT_LIST_DIR}/../graphics_d3d11/shaders/VS_*.hlsl" "${CMAKE_CURRENT_LIST_DIR}/../graphics_d3d11/shaders/PS_*.hlsl")
mmo_add_metal_shaders(graphics_metal ${builtin_shaders})

# Put the library into the shared solution folder
set_property(TARGET graphics_metal PROPERTY FOLDER "shared")
