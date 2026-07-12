// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "floor_surface_query.h"

#include "math/aabb.h"
#include "math/ray.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/scene.h"

#include <limits>

namespace mmo
{
	uint32 ResolveFloorSurfaceType(Scene& scene, const Vector3& position, const MovableObject* ignore)
	{
		// Short vertical segment: slightly above the feet down to well below any step height.
		const Vector3 start = position + Vector3(0.0f, 0.5f, 0.0f);
		const Vector3 end = position - Vector3(0.0f, 1.5f, 0.0f);
		const float maxDistanceSq = (end - start).GetSquaredLength();

		const AABB queryBounds(
			Vector3(start.x - 0.1f, end.y, start.z - 0.1f),
			Vector3(start.x + 0.1f, start.y, start.z + 0.1f));

		const auto query = scene.CreateAABBQuery(queryBounds);
		if (!query)
		{
			return 0;
		}

		query->Execute(*query);

		const Ray ray(start, end);

		uint32 resultSurfaceType = 0;
		float closestDistanceSq = std::numeric_limits<float>::max();

		for (const auto& movable : query->GetLastResult())
		{
			if (movable == ignore)
			{
				continue;
			}

			const ICollidable* collidable = movable->GetCollidable();
			if (!collidable || !collidable->IsCollidable())
			{
				continue;
			}

			CollisionResult hit{};
			if (!collidable->TestRayCollision(ray, hit))
			{
				continue;
			}

			// Reject hits beyond the segment (implementations may treat the ray as infinite).
			const float distanceSq = (hit.contactPoint - start).GetSquaredLength();
			if (distanceSq > maxDistanceSq || distanceSq >= closestDistanceSq)
			{
				continue;
			}

			closestDistanceSq = distanceSq;
			resultSurfaceType = collidable->GetSurfaceTypeAt(hit);
		}

		return resultSurfaceType;
	}
}
