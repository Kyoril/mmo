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
