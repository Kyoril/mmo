// Copyright (C) 2019, Robin Klimonow. All rights reserved.

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
		inline Point GetPosition() const { return Point(m_left, m_top); }
		inline float GetWidth() const { return m_right - m_left; }
		inline float GetHeight() const { return m_bottom - m_top; }
		inline Size GetSize() const { return Size(GetWidth(), GetHeight()); }
		inline void SetWidth(float width) { m_right = m_left + width; }
		inline void SetHeight(float height) { m_bottom = m_top + height; }
		inline void SetSize(const Size& size) { SetWidth(size.m_width); SetHeight(size.m_height); }

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
			return ((m_left == rhs.m_left) && (m_right == rhs.m_right) && (m_top == rhs.m_top) && (m_bottom == rhs.m_bottom));
		}
		inline bool operator!=(const Rect& rhs) const { return !operator==(rhs); }
		
		Rect& operator=(const Rect& rhs);

		inline Rect operator*(float scalar) const { return Rect(m_left * scalar, m_top * scalar, m_right * scalar, m_bottom * scalar); }
		inline const Rect& operator*=(float scalar) { m_left *= scalar; m_top *= scalar; m_right *= scalar; m_bottom *= scalar; return *this; }

		inline Rect operator+(const Rect&r) const { return Rect(m_left + r.m_left, m_top + r.m_top, m_right + r.m_right, m_bottom + r.m_bottom); }

	public:
		float m_left;
		float m_top;
		float m_right;
		float m_bottom;
	};
}
