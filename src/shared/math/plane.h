#pragma once

#include "vector3.h"
#include "matrix3.h"
#include "matrix4.h"
#include "aabb.h"

namespace mmo
{
	class Plane final
	{
	public:
		Vector3 normal;
		float d;

	public:
		explicit Plane()
			: normal(Vector3::Zero)
			, d(0.0f)
		{
		}

		Plane(const Vector3& normal, float d)
			: normal(normal)
			, d(d)
		{
		}

		Plane(float a, float b, float c, float d) 
			: normal(a, b, c)
			, d(d) 
		{
		}

		Plane(const Vector3& normal, const Vector3& point)
		{
			Redefine(normal, point);
		}

		Plane(const Vector3& p0, const Vector3& p1, const Vector3& p2)
		{
			Redefine(p0, p1, p2);
		}

		enum Side
		{
			NoSide,
			PositiveSide,
			NegativeSide,
			BothSides
		};

		Side GetSide(const Vector3& point) const
		{
			const float distance = GetDistance(point);

			if (distance < 0.0f) return Plane::NegativeSide;
			if (distance > 0.0f) return Plane::PositiveSide;

			return Plane::NoSide;
		}

		Side GetSide(const AABB& box) const
		{
			if (box.IsNull()) return NoSide;

			return GetSide(box.GetCenter(), box.GetSize() * 0.5f);
		}

		Side GetSide(const Vector3& center, const Vector3& halfSize) const
		{
			// Calculate the distance between box centre and the plane
			const float dist = GetDistance(center);

			// Calculate the maximise allows absolute distance for
			// the distance between box centre and plane
			const float maxAbsDist = normal.AbsDot(halfSize);

			if (dist < -maxAbsDist)
				return NegativeSide;

			if (dist > +maxAbsDist)
				return PositiveSide;

			return BothSides;
		}

		float GetDistance(const Vector3& point) const
		{
			return normal.Dot(point) + d;
		}

		void Redefine(const Vector3& p0, const Vector3& p1, const Vector3& p2)
		{
			normal = CalculateBasicFaceNormal(p0, p1, p2);
			d = -normal.Dot(p0);
		}

		void Redefine(const Vector3& normal, const Vector3& point)
		{
			this->normal = normal;
			d = -normal.Dot(point);
		}

		Vector3 ProjectVector(const Vector3& v) const
		{
			Matrix3 xform;
			xform[0][0] = 1.0f - normal.x * normal.x;
			xform[0][1] = -normal.x * normal.y;
			xform[0][2] = -normal.x * normal.z;
			xform[1][0] = -normal.y * normal.x;
			xform[1][1] = 1.0f - normal.y * normal.y;
			xform[1][2] = -normal.y * normal.z;
			xform[2][0] = -normal.z * normal.x;
			xform[2][1] = -normal.z * normal.y;
			xform[2][2] = 1.0f - normal.z * normal.z;
			return xform * v;
		}

		float Normalize()
		{
			const float length = normal.GetLength();

			if (length > 0.0f)
			{
				const float invLength = 1.0f / length;
				normal *= invLength;
				d *= invLength;
			}

			return length;
		}

		Plane operator-() const
		{
			return Plane(-(normal.x), -(normal.y), -(normal.z), -d);
		}

		bool operator==(const Plane& rhs) const
		{
			return (rhs.d == d && rhs.normal == normal);
		}

		bool operator!=(const Plane& rhs) const
		{
			return (rhs.d != d || rhs.normal != normal);
		}
	};

	inline bool Intersects(const Plane& plane, const AABB& box)
	{
		return plane.GetSide(box) == Plane::BothSides;
	}
}