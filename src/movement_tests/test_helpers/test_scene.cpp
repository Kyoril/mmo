#include "test_scene.h"

namespace mmo
{
	TestScene::TestScene()
		: Scene()
	{
	}

	std::unique_ptr<AABBSceneQuery> TestScene::CreateAABBQuery(const AABB& /*box*/)
	{
		return std::make_unique<TestAABBQuery>(*this, m_collidables);
	}
}
