#include "triangle_collidable.h"

#include <limits>

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

	bool TriangleCollidable::TestRayCollision(const Ray& ray, CollisionResult& result) const
	{
		// Mirrors Entity::TestRayCollision: report only the hit closest to the ray origin,
		// with the triangle normal derived from the winding order.
		bool anyHit = false;
		float closestDistance = std::numeric_limits<float>::max();

		for (const Triangle& tri : m_triangles)
		{
			const auto [hit, hitFraction] = ray.IntersectsTriangle(tri[0], tri[1], tri[2]);
			if (!hit)
			{
				continue;
			}

			const float distance = hitFraction * ray.GetLength();
			if (distance >= closestDistance)
			{
				continue;
			}

			closestDistance = distance;

			Vector3 normal = (tri[1] - tri[0]).Cross(tri[2] - tri[0]);
			normal.Normalize();

			result.hasCollision = true;
			result.contactPoint = ray.origin + ray.GetDirection() * distance;
			result.contactNormal = normal;
			result.penetrationDepth = distance;
			result.distance = distance;
			result.a = tri[0];
			result.b = tri[1];
			result.c = tri[2];
			anyHit = true;
		}

		return anyHit;
	}
}
