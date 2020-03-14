add_lib(frame_ui)

# Link internal libraries
target_link_libraries(frame_ui 
	graphics 
	assets 
	xml_handler)

# Link external dependencies
target_link_libraries(frame_ui
	freetype
	lua
	luabind)

# Add library to a solution folder for better organization
set_property(TARGET frame_ui PROPERTY FOLDER "shared")
