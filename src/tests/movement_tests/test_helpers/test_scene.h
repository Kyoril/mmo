#pragma once

#include "scene_graph/scene.h"
#include "test_aabb_query.h"

#include <vector>

namespace mmo
{
	/// @brief A minimal Scene subclass for movement tests.
	/// Overrides CreateAABBQuery to return a TestAABBQuery using an injected collidables list.
	class TestScene final : public Scene
	{
	public:
		TestScene();

		std::unique_ptr<AABBSceneQuery> CreateAABBQuery(const AABB& box) override;

		void SetCollidables(std::vector<MovableObject*> collidables) { m_collidables = std::move(collidables); }

		std::vector<MovableObject*> m_collidables;
	};
}
