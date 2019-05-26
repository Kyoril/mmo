// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "rect.h"


namespace mmo
{
	Rect Rect::Empty;

	Rect::Rect(float l, float t, float r, float b)
		: m_left(l)
		, m_top(t)
		, m_right(r)
		, m_bottom(b)
	{
	}

	Rect::Rect(Point pos, Size size)
		: m_left(pos.m_x)
		, m_top(pos.m_y)
		, m_right(pos.m_x + size.m_width)
		, m_bottom(pos.m_y + size.m_height)
	{
	}

	void Rect::SetPosition(const Point & pos)
	{
		Size size(GetSize());

		m_left += pos.m_x;
		m_top += pos.m_y;

		SetSize(size);
	}

	Rect Rect::GetIntersection(const Rect & rect) const
	{
		if ((m_right > rect.m_left) &&
			(m_left < rect.m_right) &&
			(m_bottom > rect.m_top) &&
			(m_top < rect.m_bottom))
		{
			Rect temp;

			temp.m_left = (m_left > rect.m_left) ? m_left : rect.m_left;
			temp.m_right = (m_right < rect.m_right) ? m_right : rect.m_right;
			temp.m_top = (m_top > rect.m_top) ? m_top : rect.m_top;
			temp.m_bottom = (m_bottom < rect.m_bottom) ? m_bottom : rect.m_bottom;

			return temp;
		}
		else
		{
			return Rect::Empty;
		}
	}

	Rect & Rect::Offset(const Point & offset)
	{
		m_left += offset.m_x;
		m_right += offset.m_x;
		m_top += offset.m_y;
		m_bottom += offset.m_y;

		return *this;
	}

	bool Rect::IsPointInRect(const Point & pt) const
	{
		if ((m_left > pt.m_x) ||
			(m_right <= pt.m_x) ||
			(m_top > pt.m_y) ||
			(m_bottom <= pt.m_y))
		{
			return false;
		}

		return true;
	}

	Rect & Rect::ConstrainSizeMax(const Size & size)
	{
		if (GetWidth() > size.m_width)
		{
			SetWidth(size.m_width);
		}

		if (GetHeight() > size.m_height)
		{
			SetHeight(size.m_height);
		}

		return *this;
	}

	Rect & Rect::ConstainSizeMin(const Size & size)
	{
		if (GetWidth() < size.m_width)
		{
			SetWidth(size.m_width);
		}

		if (GetHeight() < size.m_height)
		{
			SetHeight(size.m_height);
		}

		return *this;
	}

	Rect & Rect::ConstainSize(const Size & maxSize, const Size & minSize)
	{
		Size currentSize = GetSize();

		if (currentSize.m_width > maxSize.m_width)
		{
			SetWidth(maxSize.m_width);
		}
		else if (currentSize.m_width < minSize.m_width)
		{
			SetWidth(minSize.m_width);
		}

		if (currentSize.m_height > maxSize.m_height)
		{
			SetHeight(maxSize.m_height);
		}
		else if (currentSize.m_height < minSize.m_height)
		{
			SetHeight(minSize.m_height);
		}

		return *this;
	}

	Rect& Rect::operator=(const Rect& rhs)
	{
		m_left = rhs.m_left;
		m_top = rhs.m_top;
		m_right = rhs.m_right;
		m_bottom = rhs.m_bottom;

		return *this;
	}
}
