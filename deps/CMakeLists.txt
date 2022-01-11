
add_subdirectory(zlib)
add_subdirectory(lua)
add_subdirectory(luabind_noboost)
add_subdirectory(expat)
add_subdirectory(protobuf/cmake)

if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(freetype)
endif()
if (MMO_BUILD_TOOLS)
	add_subdirectory(lodepng)
endif()

# Move protobuf libraries into proper solution folders
set_property(TARGET libprotobuf PROPERTY FOLDER "deps")
set_property(TARGET libprotobuf-lite PROPERTY FOLDER "deps")
set_property(TARGET libprotoc PROPERTY FOLDER "deps")
set_property(TARGET protoc PROPERTY FOLDER "deps")
