// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "vector3.h"
#include "matrix4.h"

#include <ostream>
#include <istream>

namespace io
{
	class Writer;
	class Reader;
}

namespace mmo
{
	/// This class represents an axis-aligned bounding box.
	class AABB final
	{
	public:
		AABB() = default;
		AABB(const Vector3& min_, const Vector3& max_);

	public:
		/// Transforms the bounding box using the given 4x4 matrix. Keep in mind,
		/// that the result will still be an AXIS ALIGNED bounding box.
		void Transform(const Matrix4& matrix);
		
		void Combine(const Vector3& other);

		/// Combines another axis aligned bounding box with this one, so that the 
		/// new minimum is the total minimum of both boxes and the new maximum is
		/// the total maximum of both boxes.
		void Combine(const AABB& other);

		/// @brief Resets the bounding box to be empty.
		void SetNull()
		{
			min = max = Vector3::Zero;
		}

		[[nodiscard]] bool IsNull() const
		{
			return max == min;
		}

	public:
		/// Calculates the total volume of the bounding box.
		inline float GetVolume() const
		{
			const Vector3 e = max - min;
			return e.x * e.y * e.z;
		}

		/// Calculates the total surface area of the bounding box.
		inline float GetSurfaceArea() const
		{
			const Vector3 e = max - min;
			return 2.0f * (e.x*e.y + e.x*e.z + e.y*e.z);
		}

		/// Calculates the center of the bounding box.
		inline Vector3 GetCenter() const
		{
			return (max + min) * 0.5f;
		}

		/// Calculates the bounding box extents (half the size).
		inline Vector3 GetExtents() const
		{
			return GetSize() * 0.5f;
		}

		/// Calculates the size of the bounding box on all three axes.
		inline Vector3 GetSize() const
		{
			return max - min;
		}

		bool Intersects(const AABB& b2) const;

		bool IntersectsXZ(const AABB& b2) const;

		bool Intersects(const Vector3& v) const;

	public:
		/// Minimum bounding box vector.
		Vector3 min;
		/// Maximum bounding box vector.
		Vector3 max;
	};

	inline float GetBoundingRadiusFromAABB(const AABB& aabb)
	{
		Vector3 magnitude = aabb.max;
		magnitude.Ceil(-aabb.max);
		magnitude.Ceil(aabb.min);
		magnitude.Ceil(-aabb.min);
		return magnitude.GetLength();
	}

	/// Syntactic sugar for transforming an axis aligned bounding box using a 4x4 matrix.
	inline AABB operator*(const Matrix4& mat, const AABB& bb)
	{
		AABB bbox = bb;
		bbox.Transform(mat);
		return bbox;
	}

	/// Syntactic sugar for merging two axis aligned bounding boxes.
	inline AABB operator+(const AABB& bb1, const AABB& bb2)
	{
		AABB bbox = bb1;
		bbox.Combine(bb2);
		return bbox;
	}

	inline std::ostream& operator<<(std::ostream& o, const AABB& b)
	{
		return o
			<< "(Min: " << b.min << " Max: " << b.max << ")";
	}

	inline io::Writer& operator<<(io::Writer& w, const AABB& b)
	{
		return w
			<< b.min
			<< b.max;
	}

	inline io::Reader& operator>>(io::Reader& r, AABB& b)
	{
		return r
			>> b.min
			>> b.max;
	}
}
