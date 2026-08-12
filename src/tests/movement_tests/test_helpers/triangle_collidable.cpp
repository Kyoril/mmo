#include "triangle_collidable.h"

namespace mmo
{
	bool TriangleCollidable::TestCapsuleCollision(const Capsule& capsule, std::vector<CollisionResult>& results) const
	{
		bool anyHit = false;
		for (const Triangle& tri : m_triangles)
		{
			Vector3 contactPoint, contactNormal;
			float penetrationDepth, distance;
			if (CapsuleTriangleIntersection(capsule, tri[0], tri[1], tri[2],
				contactPoint, contactNormal, penetrationDepth, distance))
			{
				CollisionResult res;
				res.hasCollision = true;
				res.contactPoint = contactPoint;
				res.contactNormal = contactNormal;
				res.penetrationDepth = penetrationDepth;
				res.distance = distance;
				res.a = tri[0];
				res.b = tri[1];
				res.c = tri[2];
				results.push_back(res);
				anyHit = true;
			}
		}
		return anyHit;
	}

	bool TriangleCollidable::TestRayCollision(const Ray& /*ray*/, CollisionResult& /*result*/) const
	{
		return false;
	}
}
