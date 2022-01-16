#pragma once

#include "aabb.h"
#include "sphere.h"

namespace mmo
{
	inline bool Intersects(const Sphere& sphere, const AABB& box)
    {
        if (box.IsNull()) return false;
		
        const Vector3& center = sphere.GetCenter();
        const float radius = sphere.GetRadius();
        const Vector3& min = box.min;
        const Vector3& max = box.max;
		
		float s, d = 0;
		for (int i = 0; i < 3; ++i)
		{
			if (center[i] < min[i])
			{
				s = center[i] - min[i];
				d += s * s; 
			}
			else if(center[i] > max[i])
			{
				s = center[i] - max[i];
				d += s * s; 
			}
		}

		return d <= radius * radius;

    }
}
