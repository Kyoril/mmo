// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "border_component.h"
#include "geometry_buffer.h"
#include "geometry_helper.h"
#include "frame.h"

#include "graphics/texture_mgr.h"
#include "base/macros.h"

#include <utility>


namespace mmo
{
	BorderComponent::BorderComponent(Frame& frame, std::string filename, float borderInset)
		: FrameComponent(frame)
		, m_filename(std::move(filename))
		, m_borderInset(borderInset)
	{
		// Load the texture file
		m_texture = TextureManager::Get().CreateOrRetrieve(m_filename);
		ASSERT(m_texture);

		m_borderSizeRect = Rect(borderInset, borderInset, borderInset, borderInset);
	}

	std::unique_ptr<FrameComponent> BorderComponent::Copy() const
	{
		ASSERT(m_frame);

		auto copy = std::make_unique<BorderComponent>(*m_frame, m_filename, m_borderInset);
		CopyBaseAttributes(*copy);
		copy->m_borderInset = m_borderInset;
		copy->m_borderSizeRect = m_borderSizeRect;
		return copy;
	}

	void BorderComponent::Render(const Rect& area, const Color& color)
	{
		// Bind the texture object
		ASSERT(m_frame);
		m_frame->GetGeometryBuffer().SetActiveTexture(m_texture);

		const Rect frameRect = GetArea(area);

		// Empty point for position (right now)
		const Point position = frameRect.GetPosition();
		const Size size = frameRect.GetSize();

		// Border size
		const float borderSize = m_borderInset;

		// A rectangle that represents the content area in the frame rect
		const Rect contentRect = Rect(
			m_borderSizeRect.left, 
			m_borderSizeRect.top,
			m_texture->GetWidth() - m_borderSizeRect.right, 
			m_texture->GetHeight() - m_borderSizeRect.bottom);

		// Setup geometry

		// Top left corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			position, 
			Rect(0.0f, contentRect.bottom, contentRect.left, m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom left corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			position + Point(0.0f, size.height - borderSize),
			Rect(0.0f, 0.0f, contentRect.left, contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom right corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			position + Point(size.width - borderSize, size.height - borderSize),
			Rect(contentRect.right, 0.0f, m_texture->GetWidth(), contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Top right corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			position + Point(size.width - borderSize, 0.0f),
			Rect(contentRect.right, contentRect.bottom, m_texture->GetWidth(), m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());

		// Edges

		// Top edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			Rect(position + Point(contentRect.left, 0.0f), Size(size.width - borderSize * 2, borderSize)),
			Rect(contentRect.left, contentRect.bottom, contentRect.right, m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Left edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			Rect(position + Point(0.0f, contentRect.top), Size(borderSize, size.height - borderSize * 2)),
			Rect(0.0f, contentRect.top, contentRect.left, contentRect.bottom),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Right edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			Rect(position + Point(size.width - borderSize, contentRect.top), Size(borderSize, size.height - borderSize * 2)),
			Rect(contentRect.right, contentRect.top, m_texture->GetWidth(), contentRect.bottom),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			Rect(position + Point(borderSize, size.height - borderSize), Size(size.width - borderSize * 2, borderSize)),
			Rect(contentRect.left, 0.0f, contentRect.right, contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());


		// Center
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			color,
			Rect(position + contentRect.GetPosition(), Size(size.width - borderSize * 2, size.height - borderSize * 2)),
			contentRect,
			m_texture->GetWidth(), m_texture->GetHeight());
	}

	Size BorderComponent::GetSize() const
	{
		const uint16 realWidth = m_texture->GetWidth();
		const uint16 realHeight = m_texture->GetHeight();

		return Size(realWidth, realHeight);
	}
}
