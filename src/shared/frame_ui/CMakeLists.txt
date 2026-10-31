add_lib(frame_ui)

# Link internal libraries
target_link_libraries(frame_ui 
	graphics 
	assets 
	xml_handler)

# Link external dependencies
target_link_libraries(frame_ui
	expat
	freetype
	lua
	luabind)

if (APPLE)
	set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/clipboard_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
	target_link_libraries(frame_ui "-framework AppKit")
endif()

# Add library to a solution folder for better organization
set_property(TARGET frame_ui PROPERTY FOLDER "shared")
