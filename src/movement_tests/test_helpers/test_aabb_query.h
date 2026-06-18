#pragma once

#include "scene_graph/scene.h"

#include <vector>

namespace mmo
{
	/// @brief An AABB scene query that returns a fixed set of collidables (for tests).
	class TestAABBQuery final : public AABBSceneQuery
	{
	public:
		explicit TestAABBQuery(Scene& scene, std::vector<MovableObject*>& collidables);

		void Execute(SceneQueryListener& listener) override;

	private:
		std::vector<MovableObject*>& m_collidables;
	};
}
