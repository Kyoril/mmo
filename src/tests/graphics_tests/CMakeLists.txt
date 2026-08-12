mmo_add_test(graphics_tests
	base
	log
	math)

# float_curve.cpp is compiled in directly rather than linking the graphics library, which
# would pull the render device in and break the headless server build. The file itself has
# no render dependencies, so this keeps the curve maths under test on Linux CI.
target_sources(graphics_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/graphics/float_curve.cpp
)
