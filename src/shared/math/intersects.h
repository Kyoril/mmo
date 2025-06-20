#pragma once

#include "aabb.h"
#include "sphere.h"

#include <limits>
#include <utility>
#include <cstdlib>
#include <cmath>

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

	inline std::pair<bool, float> Intersects(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& normal, bool positiveSide, bool negativeSide)
    {
        float t;
        {
            float denom = normal.Dot(ray.GetDirection());

            // Check intersect side
            if (denom > +std::numeric_limits<float>::epsilon())
            {
                if (!negativeSide)
                    return std::pair<bool, float>(false, (float)0);
            }
            else if (denom < -std::numeric_limits<float>::epsilon())
            {
                if (!positiveSide)
                    return std::pair<bool, float>(false, (float)0);
            }
            else
            {
                // Parallel or triangle area is close to zero when
                // the plane normal not normalised.
                return std::pair<bool, float>(false, (float)0);
            }

            t = normal.Dot(a - ray.origin) / denom;

            if (t < 0)
            {
                // Intersection is behind origin
                return std::pair<bool, float>(false, (float)0);
            }
        }

        //
        // Calculate the largest area projection plane in X, Y or Z.
        //
        size_t i0, i1;
        {
            float n0 = std::abs(normal[0]);
            float n1 = std::abs(normal[1]);
            float n2 = std::abs(normal[2]);

            i0 = 1; i1 = 2;
            if (n1 > n2)
            {
                if (n1 > n0) i0 = 0;
            }
            else
            {
                if (n2 > n0) i1 = 0;
            }
        }

        //
        // Check the intersection point is inside the triangle.
        //
        {
            float u1 = b[i0] - a[i0];
            float v1 = b[i1] - a[i1];
            float u2 = c[i0] - a[i0];
            float v2 = c[i1] - a[i1];
            float u0 = t * ray.GetDirection()[i0] + ray.origin[i0] - a[i0];
            float v0 = t * ray.GetDirection()[i1] + ray.origin[i1] - a[i1];

            float alpha = u0 * v2 - u2 * v0;
            float beta = u1 * v0 - u0 * v1;
            float area = u1 * v2 - u2 * v1;

            // epsilon to avoid float precision error
            const float EPSILON = 1e-6f;

            float tolerance = -EPSILON * area;

            if (area > 0)
            {
                if (alpha < tolerance || beta < tolerance || alpha + beta > area - tolerance)
                    return std::pair<bool, float>(false, (float)0);
            }
            else
            {
                if (alpha > tolerance || beta > tolerance || alpha + beta < area - tolerance)
                    return std::pair<bool, float>(false, (float)0);
            }
        }

        return std::pair<bool, float>(true, (float)t);
    }

	inline std::pair<bool, float> Intersects(const Ray& ray, const Vector3& a, const Vector3& b, const Vector3& c, bool positiveSide, bool negativeSide)
	{
		Vector3 normal = CalculateBasicFaceNormalWithoutNormalize(a, b, c);
		return Intersects(ray, a, b, c, normal, positiveSide, negativeSide);
	}
}
