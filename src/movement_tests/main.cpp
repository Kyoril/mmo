#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "graphics/graphics_device.h"

int main(int argc, char* argv[])
{
	// GraphicsDevice must be initialized before Scene construction (Scene ctor calls Get()).
	// Use the null device for headless testing.
	mmo::GraphicsDevice::CreateNull(mmo::GraphicsDeviceDesc{});

	const int result = Catch::Session().run(argc, argv);

	mmo::GraphicsDevice::Destroy();
	return result;
}
