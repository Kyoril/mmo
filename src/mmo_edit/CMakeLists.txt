# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

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

# Editor is dpi aware
set_property(TARGET mmo_edit PROPERTY VS_DPI_AWARE "PerMonitor")
