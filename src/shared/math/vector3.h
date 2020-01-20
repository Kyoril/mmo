// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/macros.h"

#include <cmath>
#include <limits>
#include <cfloat>


namespace mmo
{
	/// This class represents a three-dimensional vector.
	class Vector3
	{
	public:
		static Vector3 Zero;
		static Vector3 UnitX;
		static Vector3 UnitY;
		static Vector3 UnitZ;

	public:

		float x, y, z;

	public:

		explicit inline  Vector3(float inX = 0.0f, float inY = 0.0f, float inZ = 0.0f)
			: x(inX)
			, y(inY)
			, z(inZ)
		{
		}

		inline Vector3(Vector3 const& other)
			: x(other.x)
			, y(other.y)
			, z(other.z)
		{
		}

		// Logarithmic
		inline Vector3& operator+=(Vector3 const& other)
		{
			x += other.x;
			y += other.y;
			z += other.z;

			return *this;
		}
		inline Vector3& operator-=(Vector3 const& other)
		{
			x -= other.x;
			y -= other.y;
			z -= other.z;

			return *this;
		}
		inline Vector3& operator*=(float scalar)
		{
			x *= scalar;
			y *= scalar;
			z *= scalar;

			return *this;
		}
		inline Vector3& operator/=(float scalar)
		{
			x /= scalar;
			y /= scalar;
			z /= scalar;

			return *this;
		}

		inline Vector3 operator+(Vector3 const& b) const
		{
			Vector3 v = *this;
			v += b;
			return v;
		}
		inline Vector3 operator-(Vector3 const& b) const
		{
			Vector3 v = *this;
			v -= b;
			return v;
		}
		inline Vector3 operator*(float b) const
		{
			Vector3 v = *this;
			v *= b;
			return v;
		}
		inline Vector3 operator/(float b) const
		{
			Vector3 v = *this;
			v /= b;
			return v;
		}

		// Comparison
		inline bool operator==(Vector3 const& other) const
		{
			return x == other.x && y == other.y && z == other.z;
		}
		inline bool operator!=(Vector3 const& other) const
		{
			return x != other.x || y != other.y || z != other.z;
		}

		// Inversion
		inline Vector3 operator-() const
		{
			return Vector3(-x, -y, -z);
		}
		inline Vector3 operator!() const
		{
			return Vector3(!x, !y, !z);
		}

		// Dot product
		inline float operator*(Vector3 const& b)
		{
			return Dot(b);
		}

	public:

		/// Caluclates the dot product of this vector and another one.
		inline float Dot(Vector3 const& other) const
		{
			return x * other.x + y * other.y + z * other.z;
		}
		/// Calculates the cross product of this vector and another one.
		inline Vector3 Cross(Vector3 const& other) const
		{
			return Vector3(y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x);
		}
		/// Gets the length of this vector.
		inline float GetLength() const
		{
			return sqrtf(GetSquaredLength());
		}
		/// Gets the squared length of this vector (more performant than GetLength, so
		/// it should be used instead of GetLength wherever possible).
		inline float GetSquaredLength() const
		{
			return x * x + y * y + z * z;
		}
		/// Normalizes this vector.
		inline float Normalize()
		{
			const float length = GetLength();
			ASSERT(length > 0.0f);
			x /= length;
			y /= length;
			z /= length;
			return length;
		}
		/// Returns a normalized copy of this vector.
		inline Vector3 NormalizedCopy() const
		{
			const float length = GetLength();
			ASSERT(length > 0.0f);

			Vector3 v = *this;
			return v / length;
		}
		/// Checks if this vector is almost equal to another vector, but allows some
		/// minimal offset for each component due to imprecise floating points.
		inline float IsNearlyEqual(const Vector3& other, float epsilon = FLT_EPSILON) const
		{
			return (abs(x - other.x) <= epsilon &&
				abs(y - other.y) <= epsilon &&
				abs(z - other.z) <= epsilon);
		}
		/// Checks whether all components of this vector are valid numbers.
		inline bool IsValid() const
		{
			// Note: if any of these is NaN for example, the == operator will always return
			// false, that's why we do checks against x == x etc. here
			return x == x && y == y && z == z;
		}
	};

}
