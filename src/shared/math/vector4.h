// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
	/// This class represents a four-dimensional vector.
	class Vector4
	{
	public:
		static Vector3 Zero;

	public:
		float x, y, z, w;

	public:

		inline Vector4()
			: x(0.0f)
			, y(0.0f)
			, z(0.0f)
			, w(0.0f)
		{
		}

		inline Vector4(float inX, float inY, float inZ, float inW)
			: x(inX)
			, y(inY)
			, z(inZ)
			, w(inW)
		{
		}

		inline Vector4(const Vector3& v3, float inW)
			: x(v3.x)
			, y(v3.y)
			, z(v3.z)
			, w(inW)
		{
		}

		inline Vector4(Vector4 const& other)
			: x(other.x)
			, y(other.y)
			, z(other.z)
			, w(other.w)
		{
		}

		[[nodiscard]] float* Ptr() { return &x; }
		[[nodiscard]] const float* Ptr() const { return &x; }

		// Logarithmic
		inline Vector4& operator+=(Vector4 const& other)
		{
			x += other.x;
			y += other.y;
			z += other.z;
			w += other.w;

			return *this;
		}
		inline Vector4& operator-=(Vector4 const& other)
		{
			x -= other.x;
			y -= other.y;
			z -= other.z;
			w -= other.w;

			return *this;
		}
		inline Vector4& operator*=(float scalar)
		{
			x *= scalar;
			y *= scalar;
			z *= scalar;
			w *= scalar;

			return *this;
		}
		inline Vector4& operator*=(const Vector4& v)
		{
			x *= v.x;
			y *= v.y;
			z *= v.z;
			w *= v.w;

			return *this;
		}
		inline Vector4& operator/=(float scalar)
		{
			x /= scalar;
			y /= scalar;
			z /= scalar;
			w /= scalar;

			return *this;
		}
		inline Vector4& operator/=(const Vector4& v)
		{
			x /= v.x;
			y /= v.y;
			z /= v.z;
			w /= v.w;

			return *this;
		}

		inline Vector4 operator+(Vector4 const& b) const
		{
			Vector4 v = *this;
			v += b;
			return v;
		}

		inline Vector4 operator-(Vector4 const& b) const
		{
			Vector4 v = *this;
			v -= b;
			return v;
		}

		inline Vector4 operator*(float b) const
		{
			Vector4 v = *this;
			v *= b;
			return v;
		}

		inline Vector4 operator*(const Vector4& b) const
		{
            return Vector4(
                x * b.x,
                y * b.y,
                z * b.z,
				w * b.w);
		}

		inline Vector4 operator/(float b) const
		{
			Vector4 v = *this;
			v /= b;
			return v;
		}

		inline Vector4 operator/(const Vector4& b) const
		{
			Vector4 v = *this;
			v /= b;
			return v;
		}

		// Comparison
		inline bool operator==(Vector4 const& other) const
		{
			return x == other.x && y == other.y && z == other.z && w == other.w;
		}

		inline bool operator!=(Vector4 const& other) const
		{
			return x != other.x || y != other.y || z != other.z || w != other.w;
		}

		// Inversion
		inline Vector4 operator-() const
		{
			return Vector4(-x, -y, -z, -w);
		}

		inline Vector4 operator!() const
		{
			return Vector4(!x, !y, !z, !w);
		}

		float operator [] (const size_t i) const
		{
			ASSERT(i < 4);
			return *(&x + i);
		}

		float& operator [] (const size_t i)
		{
			ASSERT(i < 4);
			return *(&x + i);
		}
		
		// Dot product
		inline Vector4 operator*(Vector4 const& b)
		{
			return {
				x * b.x,
				y * b.y,
				z * b.z,
				w * b.w
			};
		}
			
	public:

		/// Calculates the dot product of this vector and another one.
		[[nodiscard]] float Dot(Vector4 const& other) const
		{
			return x * other.x + y * other.y + z * other.z + w * other.w;
		}

		[[nodiscard]] float AbsDot(const Vector4& vec) const
		{
			return fabs(x * vec.x) + fabs(y * vec.y) + fabs(z * vec.z) + fabs(w * vec.w);
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
			return x * x + y * y + z * z + w * w;
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
				w /= length;
			}
			return length;
		}

		/// Returns a normalized copy of this vector.
		[[nodiscard]] Vector4 NormalizedCopy() const
		{
			float length = GetLength();
			if (length <= 0.0f) length = 0.0001f;
			//ASSERT(length > 0.0f);

			const Vector4 v = *this;
			return v / length;
		}

		/// Checks if this vector is almost equal to another vector, but allows some
		/// minimal offset for each component due to imprecise floating points.
		[[nodiscard]] bool IsNearlyEqual(const Vector4& other, const float epsilon = FLT_EPSILON) const
		{
			return
				fabs(x - other.x) <= epsilon &&
				fabs(y - other.y) <= epsilon &&
				fabs(z - other.z) <= epsilon &&
				fabs(w - other.w) <= epsilon;
		}

		/// Checks whether all components of this vector are valid numbers.
		[[nodiscard]] bool IsValid() const
		{
			// Note: if any of these is NaN for example, the == operator will always return
			// false, that's why we do checks against x == x etc. here
			return x == x && y == y && z == z && w == w;
		}
		
		[[nodiscard]] bool IsZeroLength() const
        {
	        const float squaredLength = (x * x) + (y * y) + (z * z) + (w * w);
            return (squaredLength < (1e-06 * 1e-06));
        }
	};

	inline std::ostream& operator<<(std::ostream& o, const mmo::Vector4& b)
	{
		return o
			<< "(" << b.x << ", " << b.y << ", " << b.z << ", " << b.w << ")";
	}

	inline io::Writer& operator<<(io::Writer& w, const mmo::Vector4& b)
	{
		return w
			<< io::write<float>(b.x)
			<< io::write<float>(b.y)
			<< io::write<float>(b.z)
			<< io::write<float>(b.w);
	}

	inline io::Reader& operator>>(io::Reader& r, mmo::Vector4& b)
	{
		return r
			>> io::read<float>(b.x)
			>> io::read<float>(b.y)
			>> io::read<float>(b.z)
			>> io::read<float>(b.w);
	}
}
