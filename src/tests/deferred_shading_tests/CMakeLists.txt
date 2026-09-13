mmo_add_test(deferred_shading_tests
	base
	math)

# Compiled in directly rather than linking deferred_shading, whose graphics and scene graph
# dependencies do not exist in a headless build. The water volume system has none of its own.
target_sources(deferred_shading_tests PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../shared/deferred_shading/water_volume_system.cpp
)
