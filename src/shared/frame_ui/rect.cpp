// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "rect.h"


namespace mmo
{
	Rect Rect::Empty;

	Rect::Rect(float l, float t, float r, float b)
		: left(l)
		, top(t)
		, right(r)
		, bottom(b)
	{
	}

	Rect::Rect(Point pos, Size size)
		: left(pos.x)
		, top(pos.y)
		, right(pos.x + size.width)
		, bottom(pos.y + size.height)
	{
	}

	void Rect::SetPosition(const Point & pos)
	{
		Size size(GetSize());

		left += pos.x;
		top += pos.y;

		SetSize(size);
	}

	Rect Rect::GetIntersection(const Rect & rect) const
	{
		if ((right > rect.left) &&
			(left < rect.right) &&
			(bottom > rect.top) &&
			(top < rect.bottom))
		{
			Rect temp;

			temp.left = (left > rect.left) ? left : rect.left;
			temp.right = (right < rect.right) ? right : rect.right;
			temp.top = (top > rect.top) ? top : rect.top;
			temp.bottom = (bottom < rect.bottom) ? bottom : rect.bottom;

			return temp;
		}
		else
		{
			return Rect::Empty;
		}
	}

	Rect & Rect::Offset(const Point & offset)
	{
		left += offset.x;
		right += offset.x;
		top += offset.y;
		bottom += offset.y;

		return *this;
	}

	bool Rect::IsPointInRect(const Point & pt) const
	{
		if ((left > pt.x) ||
			(right <= pt.x) ||
			(top > pt.y) ||
			(bottom <= pt.y))
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
		left = rhs.left;
		top = rhs.top;
		right = rhs.right;
		bottom = rhs.bottom;

		return *this;
	}
}
