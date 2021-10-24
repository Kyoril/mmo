// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cfloat>
#include <ostream>


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
		static Vector3 UnitScale;

	public:

		float x, y, z;

	public:

		inline Vector3()
			: x(0.0f)
			, y(0.0f)
			, z(0.0f)
		{
		}
		inline Vector3(float inX, float inY, float inZ)
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
		inline Vector3& operator*=(const Vector3& v)
		{
			x *= v.x;
			y *= v.y;
			z *= v.z;

			return *this;
		}
		inline Vector3& operator/=(float scalar)
		{
			x /= scalar;
			y /= scalar;
			z /= scalar;

			return *this;
		}
		inline Vector3& operator/=(const Vector3& v)
		{
			x /= v.x;
			y /= v.y;
			z /= v.z;

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
		inline Vector3 operator*(const Vector3& b) const
		{
            return Vector3(
                x * b.x,
                y * b.y,
                z * b.z);
		}
		inline Vector3 operator/(float b) const
		{
			Vector3 v = *this;
			v /= b;
			return v;
		}
		inline Vector3 operator/(const Vector3& b) const
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

		float operator [] (const size_t i) const
		{
			ASSERT(i < 3);
			return *(&x + i);
		}
		float& operator [] (const size_t i)
		{
			ASSERT(i < 3);
			return *(&x + i);
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
			float length = GetLength();
			if (length <= 0.0f) length = 0.0001f;
			//ASSERT(length > 0.0f);

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

	inline std::ostream& operator<<(std::ostream& o, const Vector3& b)
	{
		return o
			<< "(" << b.x << ", " << b.y << ", " << b.z << ")";
	}

	inline io::Writer& operator<<(io::Writer& w, const Vector3& b)
	{
		return w
			<< io::write<float>(b.x)
			<< io::write<float>(b.y)
			<< io::write<float>(b.z);
	}

	inline io::Reader& operator>>(io::Reader& r, Vector3& b)
	{
		return r
			>> io::read<float>(b.x)
			>> io::read<float>(b.y)
			>> io::read<float>(b.z);
	}

#ifdef MIN
#	error "MIN is defined when math/vector3.h is included! This is most likely because NOMINMAX wasn't defined before including Windows.h!"
#endif

	inline Vector3 TakeMinimum(const Vector3& a, const Vector3& b) 
	{
		return Vector3(
			std::min(a.x, b.x),
			std::min(a.y, b.y),
			std::min(a.z, b.z)
		);
	}

#ifdef MAX
#	error "MAX is defined when math/vector3.h is included! This is most likely because NOMINMAX wasn't defined before including Windows.h!"
#endif

	inline Vector3 TakeMaximum(const Vector3& a, const Vector3& b) 
	{
		return Vector3(
			std::max(a.x, b.x),
			std::max(a.y, b.y),
			std::max(a.z, b.z)
		);
	}
}
