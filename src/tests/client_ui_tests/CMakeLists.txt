mmo_add_test(client_ui_tests lua)

# The suite runs the shipped UI scripts themselves, so it needs to find them in the
# data/client submodule. Passing the directory in keeps the tests independent of the
# working directory ctest happens to start them in.
target_compile_definitions(client_ui_tests PRIVATE
	MMO_UI_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/data/client/Interface/GameUI")
