// Copyright (C) 2020, Robin Klimonow. All rights reserved.

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

		m_contentRect = Rect(borderInset, borderInset, m_texture->GetWidth() - borderInset, m_texture->GetHeight() - borderInset);
	}

	std::unique_ptr<FrameComponent> BorderComponent::Copy() const
	{
		ASSERT(m_frame);

		auto copy = std::make_unique<BorderComponent>(*m_frame, m_filename, m_borderInset);
		CopyBaseAttributes(*copy);
		return copy;
	}

	void BorderComponent::Render() const
	{
		// Bind the texture object
		ASSERT(m_frame);
		m_frame->GetGeometryBuffer().SetActiveTexture(m_texture);

		// Obtain the frame rectangle
		const Rect frameRect = GetArea();

		// Empty point for position (right now)
		const Point position = frameRect.GetPosition();
		const Size size = frameRect.GetSize();

		// Border size
		const float borderSize = m_contentRect.left;

		// Setup geometry

		// Top left corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			position, 
			Rect(0.0f, m_contentRect.bottom, m_contentRect.left, m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom left corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			position + Point(0.0f, size.height - borderSize),
			Rect(0.0f, 0.0f, m_contentRect.left, m_contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom right corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			position + Point(size.width - borderSize, size.height - borderSize),
			Rect(m_contentRect.right, 0.0f, m_texture->GetWidth(), m_contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Top right corner
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			position + Point(size.width - borderSize, 0.0f),
			Rect(m_contentRect.right, m_contentRect.bottom, m_texture->GetWidth(), m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());

		// Edges

		// Top edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			Rect(position + Point(m_contentRect.left, 0.0f), Size(size.width - borderSize * 2, borderSize)),
			Rect(m_contentRect.left, m_contentRect.bottom, m_contentRect.right, m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Left edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			Rect(position + Point(0.0f, m_contentRect.top), Size(borderSize, size.height - borderSize * 2)),
			Rect(0.0f, m_contentRect.top, m_contentRect.left, m_contentRect.bottom),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Right edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			Rect(position + Point(size.width - borderSize, m_contentRect.top), Size(borderSize, size.height - borderSize * 2)),
			Rect(m_contentRect.right, m_contentRect.top, m_texture->GetWidth(), m_contentRect.bottom),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom edge
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			Rect(position + Point(borderSize, size.height - borderSize), Size(size.width - borderSize * 2, borderSize)),
			Rect(m_contentRect.left, 0.0f, m_contentRect.right, m_contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());

		// Center
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			Rect(position + m_contentRect.GetPosition(), Size(size.width - borderSize * 2, size.height - borderSize * 2)),
			m_contentRect,
			m_texture->GetWidth(), m_texture->GetHeight());
	}

	Size BorderComponent::GetSize() const
	{
		const uint16 realWidth = m_texture->GetWidth();
		const uint16 realHeight = m_texture->GetHeight();

		return Size(realWidth, realHeight);
	}
}
