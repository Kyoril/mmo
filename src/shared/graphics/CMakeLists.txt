
# Add shared graphics library
add_lib(graphics)
target_link_libraries(graphics assets math)

# The Metal device is only built for the client and editor; headless Apple builds (tests, tools)
# create the null device only.
if (APPLE AND (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR))
	target_compile_definitions(graphics PUBLIC MMO_HAS_METAL)
endif()

# Put the library into the shared solution folder
set_property(TARGET graphics PROPERTY FOLDER "shared")
