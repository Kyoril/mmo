mmo_add_test(terrain_tests
	base
	math)

# NOTE: test_terrain.cpp is wrapped in `#if false` in its entirety and contributes no test
# cases. Enabling it needs a Scene and a MaterialManager, so it would have to move to a
# client/editor-gated suite first. Kept here, visible, rather than quietly deleted.
