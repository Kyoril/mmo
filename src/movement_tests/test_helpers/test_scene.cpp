#include "test_scene.h"

#include "graphics/graphics_device.h"

namespace mmo
{
	TestScene::TestScene()
		: Scene()
	{
		// GraphicsDevice must be initialized before Scene objects are used.
		// Guard with a static flag so we only call CreateNull once per process.
		static bool s_deviceInitialized = false;
		if (!s_deviceInitialized)
		{
			GraphicsDevice::CreateNull(GraphicsDeviceDesc{});
			s_deviceInitialized = true;
		}
	}

	std::unique_ptr<AABBSceneQuery> TestScene::CreateAABBQuery(const AABB& /*box*/)
	{
		return std::make_unique<TestAABBQuery>(*this, m_collidables);
	}
}
