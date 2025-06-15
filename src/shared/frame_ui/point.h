// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <ostream>


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Point final
	{
	public:
		static Point Zero;

	public:
		Point(float x = 0.0f, float y = 0.0f)
			: x(x)
			, y(y)
		{
		}

		Point(const Point& other)
			: x(other.x)
			, y(other.y)
		{
		}

		inline float DistanceTo(const Point& other) const
		{
			float dx = x - other.x;
			float dy = y - other.y;
			if (std::abs(dx) < FLT_EPSILON && std::abs(dy) < FLT_EPSILON)
			{
				return 0.0f;
			}
			
			return std::sqrt(dx * dx + dy * dy);
		}

	public:
		inline Point& operator+=(const Point& rhs)
		{
			x += rhs.x;
			y += rhs.y;
			return *this;
		}
		inline Point& operator-=(const Point& rhs)
		{
			x -= rhs.x;
			y -= rhs.y;
			return *this;
		}
		inline Point& operator*=(float scalar)
		{
			x *= scalar;
			y *= scalar;
			return *this;
		}
		inline Point& operator*=(const Point& rhs)
		{
			x *= rhs.x;
			y *= rhs.y;
			return *this;
		}
		inline Point& operator/=(float scalar)
		{
			ASSERT(scalar != 0.0f);
			x /= scalar;
			y /= scalar;
			return *this;
		}
		inline Point& operator/=(const Point& rhs)
		{
			ASSERT(rhs.x != 0.0f);
			ASSERT(rhs.y != 0.0f);

			x /= rhs.x;
			y /= rhs.y;
			return *this;
		}
		inline bool operator==(const Point& rhs) const
		{
			return 
				std::fabs(x - rhs.x) < FLT_EPSILON &&
				std::fabs(y - rhs.y) < FLT_EPSILON;
		}
		inline bool operator!=(const Point& rhs) const
		{
			return !(*this == rhs);
		}

	public:
		float x, y;
	};

	inline Point operator+(Point p1, const Point& p2)
	{
		p1 += p2;
		return p1;
	}
	inline Point operator-(Point p1, const Point& p2)
	{
		p1 -= p2;
		return p1;
	}
	inline Point operator*(Point p1, const Point& p2)
	{
		p1 *= p2;
		return p1;
	}
	inline Point operator/(Point p1, const Point& p2)
	{
		p1 /= p2;
		return p1;
	}
	inline Point operator*(Point p, float scalar)
	{
		p *= scalar;
		return p;
	}
	inline Point operator/(Point p, float scalar)
	{
		p /= scalar;
		return p;
	}
	
	inline std::ostream& operator<<(std::ostream& stream, const Point& point)
	{
		return stream << "("
			<< point.x << ", " << point.y << ")";
	}
}
