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
		float minVal = std::numeric_limits<float>::lowest();
		float maxVal = std::numeric_limits<float>::max();

		Vector3 corners[8] = {
			Vector3{ min.x, min.y, min.z },
			Vector3{ max.x, min.y, min.z },
			Vector3{ max.x, max.y, min.z },
			Vector3{ min.x, max.y, min.z },
			Vector3{ min.x, min.y, max.z },
			Vector3{ max.x, min.y, max.z },
			Vector3{ max.x, max.y, max.z },
			Vector3{ min.x, max.y, max.z }
		};

		min = Vector3{ maxVal, maxVal, maxVal };
		max = Vector3{ minVal, minVal, minVal };

		for (auto& v : corners)
		{
			v = matrix * v;
			min = TakeMinimum(min, v);
			max = TakeMaximum(max, v);
		}
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
