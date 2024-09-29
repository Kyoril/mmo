// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "point.h"
#include "size.h"


namespace mmo
{
	/// This class is used to render batched ui geometry.
	class Rect final
	{
	public:
		static Rect Empty;

	public:
		Rect(float l = 0.0f, float t = 0.0f, float r = 0.0f, float b = 0.0f);
		Rect(Point pos, Size size);

	public:
		inline Point GetPosition() const { return Point(left, top); }
		inline float GetWidth() const { return right - left; }
		inline float GetHeight() const { return bottom - top; }
		inline Size GetSize() const { return Size(GetWidth(), GetHeight()); }
		inline void SetWidth(float width) { right = left + width; }
		inline void SetHeight(float height) { bottom = top + height; }
		inline void SetSize(const Size& size) { SetWidth(size.width); SetHeight(size.height); }

	public:
		void SetPosition(const Point& pos);
		Rect GetIntersection(const Rect& rect) const;
		Rect& Offset(const Point& offset);
		bool IsPointInRect(const Point& pt) const;
		Rect& ConstrainSizeMax(const Size& size);
		Rect& ConstainSizeMin(const Size& size);
		Rect& ConstainSize(const Size& maxSize, const Size& minSize);

	public:
		inline bool operator==(const Rect& rhs) const
		{
			return ((left == rhs.left) && (right == rhs.right) && (top == rhs.top) && (bottom == rhs.bottom));
		}
		inline bool operator!=(const Rect& rhs) const { return !operator==(rhs); }
		
		Rect& operator=(const Rect& rhs);

		inline Rect operator*(float scalar) const { return Rect(left * scalar, top * scalar, right * scalar, bottom * scalar); }
		inline const Rect& operator*=(float scalar) { left *= scalar; top *= scalar; right *= scalar; bottom *= scalar; return *this; }

		inline Rect operator+(const Rect&r) const { return Rect(left + r.left, top + r.top, right + r.right, bottom + r.bottom); }

	public:
		float left;
		float top;
		float right;
		float bottom;
	};
}
