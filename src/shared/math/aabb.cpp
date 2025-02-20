// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "aabb.h"

#include "binary_io/reader.h"
#include "binary_io/writer.h"

namespace mmo 
{
	AABB::AABB(const Vector3 & min_, const Vector3 & max_)
		: min(min_)
		, max(max_)
	{
	}
	void AABB::Transform(const Matrix4 & matrix)
	{
		const Vector3 c = (min + max) * 0.5f;
		const Vector3 e = (max - min) * 0.5f;

		const Vector3 newCenter = matrix * c;

		Vector3 newExt;
		for (int i = 0; i < 3; ++i)
		{
			const float abs0 = fabsf(matrix[i][0]);
			const float abs1 = fabsf(matrix[i][1]);
			const float abs2 = fabsf(matrix[i][2]);
			newExt[i] = e.x * abs0 + e.y * abs1 + e.z * abs2;
		}

		min = newCenter - newExt;
		max = newCenter + newExt;
	}
	
	void AABB::Combine(const Vector3& v)
	{
		min = TakeMinimum(min, v);
		max = TakeMaximum(max, v);
	}

	void AABB::Combine(const AABB & other)
	{
		if (IsNull())
		{
			min = other.min;
			max = other.max;
		}

		min = TakeMinimum(min, other.min);
		max = TakeMaximum(max, other.max);
	}

	bool AABB::Intersects(const AABB& b2) const
	{
		if (IsNull() || b2.IsNull())
		{
			return false;	
		}
			
		// Use up to 6 separating planes
		if (max.x < b2.min.x) return false;
		if (max.y < b2.min.y) return false;
		if (max.z < b2.min.z) return false;

		if (min.x > b2.max.x) return false;
		if (min.y > b2.max.y) return false;
		if (min.z > b2.max.z) return false;
			
		return true;
	}

	bool AABB::IntersectsXZ(const AABB& b2) const
	{
		if (IsNull() || b2.IsNull())
		{
			return false;
		}

		// Use up to 4 separating planes
		if (max.x < b2.min.x) return false;
		if (max.z < b2.min.z) return false;
		if (min.x > b2.max.x) return false;
		if (min.z > b2.max.z) return false;

		return true;
	}

	bool AABB::Intersects(const Vector3& v) const
	{
		if (IsNull())
		{
			return false;
		}

		return v.x >= min.x  &&  v.x <= max.x  && 
			v.y >= min.y  &&  v.y <= max.y  && 
			v.z >= min.z  &&  v.z <= max.z;
	}
}
