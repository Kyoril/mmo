
# Include base lib
add_subdirectory(base)
add_subdirectory(log)
add_subdirectory(simple_file_format)
add_subdirectory(binary_io)
add_subdirectory(network)
add_subdirectory(sql_wrapper)
add_subdirectory(mysql_wrapper)
add_subdirectory(auth_protocol)
add_subdirectory(game_protocol)
add_subdirectory(math)
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(graphics)
	add_subdirectory(graphics_null)
	if (WIN32)
		add_subdirectory(graphics_d3d11)
	elseif(APPLE)
		add_subdirectory(graphics_metal)
	endif()
	
	add_subdirectory(frame_ui)
	add_subdirectory(scene_graph)
	add_subdirectory(terrain)
endif()
add_subdirectory(hpak)
add_subdirectory(hpak_v1_0)
add_subdirectory(base64)
add_subdirectory(http)
add_subdirectory(http_client)
add_subdirectory(virtual_dir)
add_subdirectory(updater)
add_subdirectory(update_compilation)
add_subdirectory(tex)
add_subdirectory(tex_v1_0)
add_subdirectory(assets)
add_subdirectory(xml_handler)
add_subdirectory(game)
add_subdirectory(paging)
add_subdirectory(web_services)
add_subdirectory(proto_data)

