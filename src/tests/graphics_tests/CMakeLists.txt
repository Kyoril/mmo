mmo_add_test(graphics_tests
	base
	log
	math)

# float_curve.cpp and display_settings.cpp are compiled in directly rather than linking the
# graphics library, which would pull the render device in and break the headless server build.
# Neither file has render dependencies, so this keeps their maths under test on Linux CI.
target_sources(graphics_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/graphics/float_curve.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/graphics/display_settings.cpp
)

# The material compiler tests need the graphics library, which only exists in client and editor
# builds. The server build keeps the rest of this suite and leaves them out.
if (MMO_BUILD_CLIENT OR MMO_BUILD_EDITOR)
	target_link_libraries(graphics_tests graphics graphics_null assets tex tex_v1_0 binary_io_hdrs ${OPENSSL_LIBRARIES})
	if (WIN32)
		target_link_libraries(graphics_tests graphics_d3d11)
	elseif (TARGET graphics_metal)
		target_link_libraries(graphics_tests graphics_metal)
	endif()
else()
	get_target_property(graphicsTestSources graphics_tests SOURCES)
	list(FILTER graphicsTestSources EXCLUDE REGEX "material_compiler_platform_tests\.cpp$")
	set_property(TARGET graphics_tests PROPERTY SOURCES ${graphicsTestSources})
endif()
