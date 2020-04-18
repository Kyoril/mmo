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
	void AABB::Combine(const AABB & other)
	{
		min = TakeMinimum(min, other.min);
		max = TakeMaximum(max, other.max);
	}
}
