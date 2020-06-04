
# Add shared graphics library
add_proto_lib(data)
target_link_libraries(data assets)

# Put the library into the shared solution folder
set_property(TARGET data PROPERTY FOLDER "shared")
