// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <cfloat>
#include <cmath>

#include "base/macros.h"


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Size final
	{
	public:
		static Size Zero;

	public:
		Size(float width = 0.0f, float height = 0.0f) 
			: width(width)
			, height(height)
		{}

	public:
		inline Size& operator+=(const Size& rhs) noexcept
		{
			width += rhs.width;
			height += rhs.height;
			return *this;
		}
		inline Size& operator-=(const Size& rhs) noexcept
		{
			width -= rhs.width;
			height -= rhs.height;
			return *this;
		}
		inline Size& operator*=(float scalar) noexcept
		{
			width *= scalar;
			height *= scalar;
			return *this;
		}
		inline Size& operator*=(const Size& rhs) noexcept
		{
			width *= rhs.width;
			height *= rhs.height;
			return *this;
		}
		inline Size& operator/=(float scalar)
		{
			ASSERT(scalar != 0.0f);
			width /= scalar;
			height /= scalar;
			return *this;
		}
		inline Size& operator/=(const Size& rhs)
		{
			ASSERT(rhs.width != 0.0f);
			ASSERT(rhs.height != 0.0f);

			width /= rhs.width;
			height /= rhs.height;
			return *this;
		}
		inline bool operator==(const Size& rhs) const
		{
			return 
				std::fabs(width - rhs.width) < FLT_EPSILON &&
				std::fabs(height - rhs.height) < FLT_EPSILON;
		}
		inline bool operator!=(const Size& rhs) const
		{
			return !(*this == rhs);
		}

	public:
		float width;
		float height;
	};

	inline Size operator+(Size p1, const Size& p2)
	{
		p1 += p2;
		return p1;
	}
	inline Size operator-(Size p1, const Size& p2)
	{
		p1 -= p2;
		return p1;
	}
	inline Size operator*(Size p1, const Size& p2)
	{
		p1 *= p2;
		return p1;
	}
	inline Size operator/(Size p1, const Size& p2)
	{
		p1 /= p2;
		return p1;
	}
	inline Size operator*(Size p, float scalar)
	{
		p *= scalar;
		return p;
	}
	inline Size operator/(Size p, float scalar)
	{
		p /= scalar;
		return p;
	}
}
