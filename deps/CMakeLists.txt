
add_subdirectory(zlib)
add_subdirectory(lua)
add_subdirectory(luabind_noboost)
add_subdirectory(freetype)
add_subdirectory(expat)
add_subdirectory(protobuf/cmake)

# Move protobuf libraries into proper solution folders
set_property(TARGET libprotobuf PROPERTY FOLDER "deps")
set_property(TARGET libprotobuf-lite PROPERTY FOLDER "deps")
set_property(TARGET libprotoc PROPERTY FOLDER "deps")
set_property(TARGET protoc PROPERTY FOLDER "deps")
