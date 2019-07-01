add_lib(frame_ui)
target_link_libraries(frame_ui graphics assets xml_handler freetype)
set_property(TARGET frame_ui PROPERTY FOLDER "shared")
