// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "border_component.h"
#include "geometry_buffer.h"
#include "geometry_helper.h"

#include "graphics/texture_mgr.h"


namespace mmo
{
	BorderComponent::BorderComponent(const std::string& filename, float borderInset)
		: FrameComponent()
	{
		// Load the texture file
		m_texture = TextureManager::Get().CreateOrRetrieve(filename);
		ASSERT(m_texture);

		m_contentRect = Rect(borderInset, borderInset, m_texture->GetWidth() - borderInset, m_texture->GetHeight() - borderInset);
	}

	void BorderComponent::Render(Frame& frame) const
	{
		// Bind the texture object
		frame.GetGeometryBuffer().SetActiveTexture(m_texture);

		// Empty point for position (right now)
		const Point position;
		const Size size = GetSize();

		// Setup geometry

		// Corners

		// Top left corner
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(), 
			position, 
			Rect(0.0f, m_contentRect.bottom, m_contentRect.left, m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom left corner
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(0.0f, m_contentRect.top + m_contentRect.GetHeight()),
			Rect(0.0f, 0.0f, m_contentRect.left, m_contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom right corner
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(m_contentRect.left + m_contentRect.GetWidth(), m_contentRect.top + m_contentRect.GetHeight()),
			Rect(m_contentRect.right, 0.0f, m_texture->GetWidth(), m_contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Top right corner
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(m_contentRect.left + m_contentRect.GetWidth(), 0.0f),
			Rect(m_contentRect.right, m_contentRect.bottom, m_texture->GetWidth(), m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());

		// Edges

		// Top edge
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(m_contentRect.left, 0.0f),
			Rect(m_contentRect.left, m_contentRect.bottom, m_contentRect.right, m_texture->GetHeight()),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Left edge
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(0.0f, m_contentRect.top),
			Rect(0.0f, m_contentRect.top, m_contentRect.left, m_contentRect.bottom),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Right edge
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(m_contentRect.left + m_contentRect.GetWidth(), m_contentRect.top),
			Rect(m_contentRect.right, m_contentRect.top, m_texture->GetWidth(), m_contentRect.bottom),
			m_texture->GetWidth(), m_texture->GetHeight());
		// Bottom edge
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + Point(m_contentRect.left, m_contentRect.top + m_contentRect.GetHeight()),
			Rect(m_contentRect.left, 0.0f, m_contentRect.right, m_contentRect.top),
			m_texture->GetWidth(), m_texture->GetHeight());

		// Center
		GeometryHelper::CreateRect(frame.GetGeometryBuffer(),
			position + m_contentRect.GetPosition(),
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
