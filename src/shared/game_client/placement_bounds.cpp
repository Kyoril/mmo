// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "placement_bounds.h"

#include "math/matrix4.h"

namespace mmo
{
	AABB TransformPlacementBounds(const AABB& bounds, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
	{
		if (bounds.IsNull())
		{
			return bounds;
		}

		Matrix4 transform;
		transform.MakeTransform(position, scale, rotation);

		AABB result = bounds;
		result.Transform(transform);
		return result;
	}

	AABB CombineChildBounds(const AABB& parent, const AABB& childBounds, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
	{
		if (childBounds.IsNull())
		{
			return parent;
		}

		const AABB placed = TransformPlacementBounds(childBounds, position, rotation, scale);
		if (parent.IsNull())
		{
			return placed;
		}

		AABB combined = parent;
		combined.Combine(placed);
		return combined;
	}
}
