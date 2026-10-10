mmo_add_test(nav_build_tests
	base
	math
	Recast)

# The rasterizer is compiled in directly: linking nav_build would pull in its scene graph and
# graphics dependencies, which do not exist in a headless build.
target_sources(nav_build_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/nav_build/rasterize.cpp
)
