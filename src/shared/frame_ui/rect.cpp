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
		: m_left(pos.x)
		, m_top(pos.y)
		, m_right(pos.x + size.width)
		, m_bottom(pos.y + size.height)
	{
	}

	void Rect::SetPosition(const Point & pos)
	{
		Size size(GetSize());

		m_left += pos.x;
		m_top += pos.y;

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
		m_left += offset.x;
		m_right += offset.x;
		m_top += offset.y;
		m_bottom += offset.y;

		return *this;
	}

	bool Rect::IsPointInRect(const Point & pt) const
	{
		if ((m_left > pt.x) ||
			(m_right <= pt.x) ||
			(m_top > pt.y) ||
			(m_bottom <= pt.y))
		{
			return false;
		}

		return true;
	}

	Rect & Rect::ConstrainSizeMax(const Size & size)
	{
		if (GetWidth() > size.width)
		{
			SetWidth(size.width);
		}

		if (GetHeight() > size.height)
		{
			SetHeight(size.height);
		}

		return *this;
	}

	Rect & Rect::ConstainSizeMin(const Size & size)
	{
		if (GetWidth() < size.width)
		{
			SetWidth(size.width);
		}

		if (GetHeight() < size.height)
		{
			SetHeight(size.height);
		}

		return *this;
	}

	Rect & Rect::ConstainSize(const Size & maxSize, const Size & minSize)
	{
		Size currentSize = GetSize();

		if (currentSize.width > maxSize.width)
		{
			SetWidth(maxSize.width);
		}
		else if (currentSize.width < minSize.width)
		{
			SetWidth(minSize.width);
		}

		if (currentSize.height > maxSize.height)
		{
			SetHeight(maxSize.height);
		}
		else if (currentSize.height < minSize.height)
		{
			SetHeight(minSize.height);
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
