# One test executable per library. Each subdirectory holds that library's tests and a
# CMakeLists.txt whose only content is an mmo_add_test() call naming the libraries under
# test. Suites that need the renderer are gated on a client or editor build; everything
# else has to keep working on the headless Linux server build.

add_subdirectory(auth_protocol_tests)
add_subdirectory(base64_tests)
add_subdirectory(base_tests)
add_subdirectory(binary_io_tests)
add_subdirectory(deferred_shading_tests)
add_subdirectory(frame_ui_tests)
add_subdirectory(game_client_tests)
add_subdirectory(game_protocol_tests)
add_subdirectory(game_server_tests)
add_subdirectory(game_tests)
add_subdirectory(graphics_tests)
add_subdirectory(hpak_tests)
add_subdirectory(login_server_tests)
add_subdirectory(math_tests)
add_subdirectory(network_tests)
add_subdirectory(realm_server_tests)
add_subdirectory(simple_file_format_tests)
add_subdirectory(terrain_io_tests)
add_subdirectory(terrain_tests)

# Suites that need a graphics device and cannot run on the headless server build.
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	add_subdirectory(movement_tests)
	add_subdirectory(scene_graph_tests)
endif()
