// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "font_image.h"
#include "font_imageset.h"


namespace mmo
{
	FontImage::FontImage()
		: m_owner(nullptr)
	{
	}

	FontImage::FontImage(const FontImageset & owner, const Rect & area, const Point & renderOffset, float horzScaling, float vertScaling)
		: m_owner(&owner)
		, m_area(area)
		, m_offset(renderOffset)
	{
#define PixelAligned(x)	( (float)(int)(( x ) + (( x ) > 0.0f ? 0.5f : -0.5f)) )

		m_scaledSize = Size(PixelAligned(m_area.GetWidth() * horzScaling), PixelAligned(m_area.GetHeight() * vertScaling));
		m_scaledOffset = Point(PixelAligned(m_offset.x * horzScaling), PixelAligned(m_offset.y * vertScaling));

#undef PixelAligned
	}

	const Rect & FontImage::GetSourceTextureArea() const
	{
		return m_area;
	}

	void FontImage::Draw(const Point & position, const Size & size, GeometryBuffer & buffer, argb_t color) const
	{
		if (!m_owner)
			return;

		m_owner->Draw(m_area, Rect(Point(position.x + m_scaledOffset.x, position.y + m_scaledOffset.y), size), buffer, color);
	}
}
