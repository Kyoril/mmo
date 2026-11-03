# Release content cooking (material shaders per platform, later more). No device dependency, so the
# cook tool runs headless on Linux; graphics compilers are passed in by the caller.
add_lib(content_cook)
target_link_libraries(content_cook material_graph graphics assets binary_io_hdrs math base log)

# Put the library into the shared solution folder
set_property(TARGET content_cook PROPERTY FOLDER "shared")
