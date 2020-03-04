// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "image_component.h"
#include "geometry_buffer.h"

#include "graphics/texture_mgr.h"


namespace mmo
{
	ImageComponent::ImageComponent(const std::string& filename)
		: FrameComponent()
		, m_width(0)
		, m_height(0)
	{
		m_texture = TextureManager::Get().CreateOrRetrieve(filename);
	}

	void ImageComponent::Render(Frame& frame) const
	{
		// Bind the texture object
		frame.GetGeometryBuffer().SetActiveTexture(m_texture);
		
		// Determine width and height
		float w = m_width; 
		if (m_width == 0) w = m_texture->GetWidth();
		float h = m_height; 
		if (m_height == 0) h = m_texture->GetHeight();

		// Setup geometry
		GeometryBuffer::Vertex vertices[6]{
			// First triangle
			{ { 0.0f,    h, 0.0f }, 0xffffffff, { 0.0f, 0.0f } },
			{ { 0.0f, 0.0f, 0.0f }, 0xffffffff, { 0.0f, 1.0f } },
			{ {    w, 0.0f, 0.0f }, 0xffffffff, { 1.0f, 1.0f } },
			// Second triangle
			{ {    w, 0.0f, 0.0f }, 0xffffffff, { 1.0f, 1.0f } },
			{ {    w,    h, 0.0f }, 0xffffffff, { 1.0f, 0.0f } },
			{ { 0.0f,    h, 0.0f }, 0xffffffff, { 0.0f, 0.0f } }
		};

		// Append vertices
		frame.GetGeometryBuffer().AppendGeometry(vertices, 6);
	}

	Size ImageComponent::GetSize() const
	{
		uint16 realWidth = m_width;
		if (realWidth == 0) realWidth = m_texture->GetWidth();

		uint16 realHeight = m_height;
		if (realHeight == 0) realHeight = m_texture->GetHeight();

		return Size(realWidth, realHeight);
	}
}
