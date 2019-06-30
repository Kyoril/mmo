
# Add executable
add_gui_exe(mmo_edit)

target_link_libraries(mmo_edit 
	base 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	auth_protocol 
	graphics 
	math 
	virtual_dir 
	hpak_v1_0 
	tex_v1_0 
	assets
	frame_ui)

target_link_libraries(mmo_edit ${OPENSSL_LIBRARIES})
set_property(TARGET mmo_edit PROPERTY FOLDER "editor")
