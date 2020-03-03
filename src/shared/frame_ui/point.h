// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/macros.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Point final
	{
	public:
		static Point Zero;

	public:
		Point(float x = 0.0f, float y = 0.0f) noexcept
			: x(x)
			, y(y)
		{
		}

		Point(const Point& other) noexcept
			: x(other.x)
			, y(other.y)
		{
		}

	public:
		inline Point& operator+=(const Point& rhs) noexcept
		{
			x += rhs.x;
			y += rhs.y;
			return *this;
		}
		inline Point& operator-=(const Point& rhs) noexcept
		{
			x -= rhs.x;
			y -= rhs.y;
			return *this;
		}
		inline Point& operator*=(float scalar) noexcept
		{
			x *= scalar;
			y *= scalar;
			return *this;
		}
		inline Point& operator/=(float scalar)
		{
			ASSERT(scalar != 0.0f);
			x /= scalar;
			y /= scalar;
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
}
