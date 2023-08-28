#pragma once

#include "vector3.h"
#include "aabb.h"
#include "base/macros.h"

#include <utility>
#include <cstdlib>
#include <cmath>

namespace mmo
{
	struct Ray final
	{
		/// @brief Starting point of the ray.
		Vector3 origin;

		/// @brief Destination point of the ray.
		Vector3 destination;

		/// @brief Normalized direction of the ray.
		Vector3 direction;

		/// @brief Perctual hit distance or 1.0f if nothing was hit.
		float hitDistance = 1.0f;

		explicit Ray()
		{
		}

		/// @brief Initializes the ray by providing a start point and an end point. This
		/// will automatically calculate the direction of the ray.
		/// @param start The starting point of the ray.
		/// @param end The ending point of the ray used to calculate the direction.
		explicit Ray(const Vector3& start, const Vector3& end)
			: origin(start)
			, destination(end)
		{
			ASSERT(origin != destination);
			direction = (destination - origin);
			direction.Normalize();
		}

		/// @brief Initializes the ray by providing an origin, a direction and a maximum distance.
		/// @param start The starting point of the ray.
		/// @param dir The normalized direction of the ray.
		/// @param maxDistance The maximum distance of the ray. Used to calculate the ending point.
		///                    Has to be >= 0.
		explicit Ray(Vector3 start, Vector3 dir, float maxDistance)
			: origin(std::move(start))
			, direction(std::move(dir))
		{
			ASSERT(maxDistance > 0.0f);
			ASSERT(direction.GetLength() >= 0.9999f && direction.GetLength() <= 1.0001f);
			destination = origin + (direction * maxDistance);
		}
		/// @brief Gets the vector representation of this ray.
		/// @returns Vector representation of this ray.
		Vector3 GetVector() const 
		{
			return destination - origin;
		}

		/// @brief Gets the maximum length of the vector (so distance between origin and destination).
		/// @returns Maximum length of the ray in world units.
		float GetLength() const
		{
			return GetVector().GetLength();
		}

		/// @brief Checks whether this ray intersects with a triangle.
		/// @param a The first vertex of the triangle.
		/// @param b The second vertex of the triangle.
		/// @param c The third vertex of the triangle.
		/// @returns A pair of true and the hit distance, if the ray intersects. Otherwise,
		///          a pair of false and 0 is returned.
		std::pair<bool, float> IntersectsTriangle(const Vector3& a, const Vector3& b, const Vector3& c, bool ignoreBackface = false)
		{
			const float upscaleFactor = 100.0f;

			Vector3 rayDir = direction * upscaleFactor;
			Vector3 v0 = a * upscaleFactor;
			Vector3 v1 = b * upscaleFactor - v0;
			Vector3 v2 = c * upscaleFactor - v0;

			Vector3 p = rayDir.Cross(v2);
			float det = v1.Dot(p);

			if ((ignoreBackface && det < 1e-5) || std::abs(det) < 1e-5)
			{
				return { false, 0.0f };
			}

			Vector3 t = origin * upscaleFactor - v0;
			float e1 = t.Dot(p) / det;

			if (e1 < 0.0f || e1 > 1.0f) 
			{
				return{ false, 0.0f };
			}

			Vector3 q = t.Cross(v1);
			float e2 = rayDir.Dot(q) / det;

			if (e2 < 0.0f || (e1 + e2) > 1.0f)
			{
				return{ false, 0.0f };
			}

			float d = v2.Dot(q) / det;
			if (d < 1e-5)
			{
				return{ false, 0.0f };
			}

			return { true, d / GetLength() };
		}

		/// @brief Checks whether this ray intersects with a bounding box.
		/// @param box The bounding box to check.
		/// @returns First paramater is true if ray cast intersects. Second parameter returns the
		///          hit distance, which can be used to calculate the hit point (if first parameter is true).
		std::pair<bool, float> intersectsAABB(const AABB& box) const
		{
			const Vector3 invDir{
				1.0f / direction.x,
				1.0f / direction.y,
				1.0f / direction.z
			};

			float t1 = (box.min.x - origin.x) * invDir.x;
			float t2 = (box.max.x - origin.x) * invDir.x;
			float t3 = (box.min.y - origin.y) * invDir.y;
			float t4 = (box.max.y - origin.y) * invDir.y;
			float t5 = (box.min.z - origin.z) * invDir.z;
			float t6 = (box.max.z - origin.z) * invDir.z;

			float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
			float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

			// if tmax < 0, ray (line) is intersecting AABB, but whole AABB is behind us
			if (tmax < 0)
			{
				return std::make_pair(false, 0.0f);
			}

			// if tmin > tmax, ray doesn't intersect AABB
			if (tmin > tmax)
			{
				return std::make_pair(false, 0.0f);
			}

			return std::make_pair(true, tmin / GetLength());
		}
	};
}