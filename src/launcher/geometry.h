// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <algorithm>

namespace mmo
{
	/// A point in a top-down coordinate space with the origin in the upper left corner.
	struct Point
	{
		int32 x = 0;
		int32 y = 0;
	};

	struct Size
	{
		int32 width = 0;
		int32 height = 0;
	};

	/// A half open rectangle: left and top are inside, right and bottom are not.
	struct Rect
	{
		int32 left = 0;
		int32 top = 0;
		int32 right = 0;
		int32 bottom = 0;

		constexpr int32 GetWidth() const { return right - left; }
		constexpr int32 GetHeight() const { return bottom - top; }
		constexpr bool IsEmpty() const { return right <= left || bottom <= top; }

		constexpr bool Contains(const Point& p) const
		{
			return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
		}
	};

	/// Per edge distances, used for nine slice insets and rectangle inflation.
	struct Insets
	{
		int32 left = 0;
		int32 top = 0;
		int32 right = 0;
		int32 bottom = 0;
	};

	constexpr Rect Intersect(const Rect& a, const Rect& b)
	{
		return Rect{
			std::max(a.left, b.left),
			std::max(a.top, b.top),
			std::min(a.right, b.right),
			std::min(a.bottom, b.bottom)
		};
	}

	constexpr Rect Offset(const Rect& r, const int32 dx, const int32 dy)
	{
		return Rect{ r.left + dx, r.top + dy, r.right + dx, r.bottom + dy };
	}

	/// Grows a rectangle by `amount` on every edge. A negative amount shrinks it.
	constexpr Rect Inflate(const Rect& r, const int32 amount)
	{
		return Rect{ r.left - amount, r.top - amount, r.right + amount, r.bottom + amount };
	}
}
