#include "test_aabb_query.h"

namespace mmo
{
	TestAABBQuery::TestAABBQuery(Scene& scene, std::vector<MovableObject*>& collidables)
		: AABBSceneQuery(scene)
		, m_collidables(collidables)
	{
	}

	void TestAABBQuery::Execute(SceneQueryListener& listener)
	{
		for (MovableObject* obj : m_collidables)
		{
			if (obj)
			{
				listener.QueryResult(*obj);
			}
		}
	}
}
