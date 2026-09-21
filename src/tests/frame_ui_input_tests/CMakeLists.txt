mmo_add_test(frame_ui_input_tests frame_ui graphics_null log math expat tex tex_v1_0)

if (WIN32)
	target_link_libraries(frame_ui_input_tests graphics_d3d11)
endif()
