# Build time tool: converts the engine's HLSL shaders to Metal shader blobs embedded as C++ headers.
add_exe(hlsl2msl)
target_link_libraries(hlsl2msl msl_cross)
set_property(TARGET hlsl2msl PROPERTY FOLDER "tools")
