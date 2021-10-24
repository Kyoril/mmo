# We require 3.16 for the VS_DPI_AWARE option below, otherwise the application will be scaled by windows
# which will result in blurry madness on high dpi monitors
cmake_minimum_required(VERSION 3.16)

# Add executable
add_gui_exe(mmo_client)

# Link internal libraries
target_link_libraries(mmo_client 
	base 
	log 
	simple_file_format_hdrs 
	binary_io_hdrs 
	network_hdrs 
	auth_protocol 
	game_protocol
	graphics 
	math 
	virtual_dir 
	hpak_v1_0 
	tex_v1_0 
	assets
	frame_ui
	expat
	game
	mesh
	mesh_v1_0
	scene_graph)

# Link external dependency libraries
target_link_libraries(mmo_client lua)
target_link_libraries(mmo_client ${OPENSSL_LIBRARIES})

# Add project to a solution folder for better organization
set_property(TARGET mmo_client PROPERTY FOLDER "client")

# Client is dpi aware
set_property(TARGET mmo_client PROPERTY VS_DPI_AWARE "PerMonitor")
