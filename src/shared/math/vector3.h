// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cfloat>
#include <ostream>

#include "clamp.h"
#include "math_utils.h"
#include "radian.h"


namespace mmo
{
	class Quaternion;

	/// This class represents a three-dimensional vector.
	class Vector3
	{
	public:
		static Vector3 Zero;
		static Vector3 UnitX;
		static Vector3 UnitY;
		static Vector3 UnitZ;
		static Vector3 UnitScale;
		static Vector3 NegativeUnitX;
		static Vector3 NegativeUnitY;
		static Vector3 NegativeUnitZ;

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

		[[nodiscard]] float* Ptr() { return &x; }
		[[nodiscard]] const float* Ptr() const { return &x; }

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
			ASSERT(scalar != 0.0f);

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
		inline Vector3 operator*(Vector3 const& b)
		{
			return {
				x * b.x,
				y * b.y,
				z * b.z
			};
		}
			
	public:

		/// Calculates the dot product of this vector and another one.
		[[nodiscard]] float Dot(Vector3 const& other) const
		{
			return x * other.x + y * other.y + z * other.z;
		}

		[[nodiscard]] float AbsDot(const Vector3& vec) const
		{
			return fabs(x * vec.x) + fabs(y * vec.y) + fabs(z * vec.z);
		}

		/// Calculates the cross product of this vector and another one.
		[[nodiscard]] Vector3 Cross(Vector3 const& other) const
		{
			return {y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
		}

		/// Gets the length of this vector.
		[[nodiscard]] float GetLength() const
		{
			return sqrtf(GetSquaredLength());
		}

		/// Gets the squared length of this vector (more performant than GetLength, so
		/// it should be used instead of GetLength wherever possible).
		[[nodiscard]] float GetSquaredLength() const
		{
			return x * x + y * y + z * z;
		}

		/// Normalizes this vector.
		float Normalize()
		{
			const float length = GetLength();
			if (length > 0.0f)
			{
				x /= length;
				y /= length;
				z /= length;
			}
			return length;
		}

		/// Returns a normalized copy of this vector.
		[[nodiscard]] Vector3 NormalizedCopy() const
		{
			float length = GetLength();
			if (length <= 0.0f) length = 0.0001f;
			//ASSERT(length > 0.0f);

			const Vector3 v = *this;
			return v / length;
		}

		[[nodiscard]] float GetDistanceTo(const Vector3& rhs) const
		{
			return (*this - rhs).GetLength();
		}

		[[nodiscard]] float GetSquaredDistanceTo(const Vector3& rhs) const
		{
			return (*this - rhs).GetSquaredLength();
		}

		/// Checks if this vector is almost equal to another vector, but allows some
		/// minimal offset for each component due to imprecise floating points.
		[[nodiscard]] bool IsNearlyEqual(const Vector3& other, const float epsilon = FLT_EPSILON) const
		{
			return
				fabs(x - other.x) <= epsilon &&
				fabs(y - other.y) <= epsilon &&
				fabs(z - other.z) <= epsilon;
		}

		/// Checks whether all components of this vector are valid numbers.
		[[nodiscard]] bool IsValid() const
		{
			// Note: if any of these is NaN for example, the == operator will always return
			// false, that's why we do checks against x == x etc. here
			return x == x && y == y && z == z;
		}
		
		[[nodiscard]] Radian AngleBetween(const Vector3& dest) const
		{
			float lenProduct = GetLength() * dest.GetLength();

			// Divide by zero check
			if(lenProduct < 1e-6f)
			{
				lenProduct = 1e-6f;	
			}

			float f = Dot(dest) / lenProduct;
			f = Clamp(f, -1.0f, 1.0f);
			return ACos(f);
		}

		[[nodiscard]] bool IsZeroLength() const
        {
	        const float squaredLength = (x * x) + (y * y) + (z * z);
            return (squaredLength < (1e-06 * 1e-06));
        }

		[[nodiscard]] Quaternion GetRotationTo(const Vector3& dest, const Vector3& fallbackAxis = Zero) const;

		void Ceil(const Vector3& other)
		{
			if (other.x > x) x = other.x;
			if (other.y > y) y = other.y;
			if (other.z > z) z = other.z;
		}

		[[nodiscard]] bool IsCloseTo(const Vector3& rhs, const float tolerance = 1e-03f) const
		{
			return GetSquaredDistanceTo(rhs) <=
				(GetSquaredLength() + rhs.GetSquaredLength()) * tolerance;
		}

		[[nodiscard]] Vector3 Lerp(const Vector3& target, float t) const
		{
			if (t <= 0.0f) {
				return *this;
			}

			if (t >= 1.0f) {
				return target;
			}

			// NaN check
			ASSERT(t == t);
			ASSERT(x == x);
			ASSERT(y == y);
			ASSERT(z == z);
			ASSERT(target.x == target.x);
			ASSERT(target.y == target.y);
			ASSERT(target.z == target.z);
			return *this + (target - *this) * t;
		}
	};

	inline std::ostream& operator<<(std::ostream& o, const mmo::Vector3& b)
	{
		return o
			<< "(" << b.x << ", " << b.y << ", " << b.z << ")";
	}

	inline io::Writer& operator<<(io::Writer& w, const mmo::Vector3& b)
	{
		return w
			<< io::write<float>(b.x)
			<< io::write<float>(b.y)
			<< io::write<float>(b.z);
	}

	inline io::Reader& operator>>(io::Reader& r, mmo::Vector3& b)
	{
		return r
			>> io::read<float>(b.x)
			>> io::read<float>(b.y)
			>> io::read<float>(b.z);
	}

	inline Vector3 TakeMinimum(const Vector3& a, const Vector3& b) 
	{
		return Vector3(
			std::min(a.x, b.x),
			std::min(a.y, b.y),
			std::min(a.z, b.z)
		);
	}

	inline Vector3 TakeMaximum(const Vector3& a, const Vector3& b) 
	{
		return Vector3(
			std::max(a.x, b.x),
			std::max(a.y, b.y),
			std::max(a.z, b.z)
		);
	}

	inline Vector3 CalculateBasicFaceNormal(const Vector3& v1, const Vector3& v2, const Vector3& v3)
	{
		Vector3 normal = (v2 - v1).Cross(v3 - v1);
		normal.Normalize();
		return normal;
	}

	inline Vector3 CalculateBasicFaceNormalWithoutNormalize(
		const Vector3& v1, const Vector3& v2, const Vector3& v3)
	{
		return (v2 - v1).Cross(v3 - v1);
	}
}
