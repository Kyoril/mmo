// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "image_component.h"
#include "geometry_buffer.h"
#include "geometry_helper.h"
#include "frame.h"

#include "base/utilities.h"
#include "base/macros.h"
#include "graphics/texture_mgr.h"

#include <utility>


namespace mmo
{
	ImageComponent::ImageComponent(Frame& frame, std::string filename)
		: FrameComponent(frame)
		, m_filename(std::move(filename))
		, m_width(0)
		, m_height(0)
		, m_tiling(ImageTilingMode::None)
	{
		m_texture = TextureManager::Get().CreateOrRetrieve(m_filename);
	}

	std::unique_ptr<FrameComponent> ImageComponent::Copy() const
	{
		ASSERT(m_frame);
		return std::make_unique<ImageComponent>(*m_frame, m_filename);
	}
	
	void ImageComponent::Render() const
	{
		// Bind the texture object
		ASSERT(m_frame);
		m_frame->GetGeometryBuffer().SetActiveTexture(m_texture);
		
		const Rect frameRect = GetArea();

		// Default source rect encapsules the whole image area
		Rect srcRect{ 0.0f, 0.0f, static_cast<float>(m_texture->GetWidth()), static_cast<float>(m_texture->GetHeight())};

		// Apply tiling
		if (m_tiling == ImageTilingMode::Horizontally ||
			m_tiling == ImageTilingMode::Both)
		{
			const float factorX = frameRect.GetWidth() / static_cast<float>(m_texture->GetWidth());
			srcRect.SetWidth(factorX * srcRect.GetWidth());
		}
		if (m_tiling == ImageTilingMode::Vertically ||
			m_tiling == ImageTilingMode::Both)
		{
			const float factorY = frameRect.GetHeight() / static_cast<float>(m_texture->GetHeight());
			srcRect.SetWidth(factorY * srcRect.GetHeight());
		}

		// Create the rectangle
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			frameRect,
			srcRect,
			m_texture->GetWidth(), m_texture->GetHeight());
	}

	void ImageComponent::SetTilingMode(ImageTilingMode mode)
	{
		m_tiling = mode;
	}

	Size ImageComponent::GetSize() const
	{
		uint16 realWidth = m_width;
		if (realWidth == 0) realWidth = m_texture->GetWidth();

		uint16 realHeight = m_height;
		if (realHeight == 0) realHeight = m_texture->GetHeight();

		return Size(realWidth, realHeight);
	}

	ImageTilingMode ImageTilingModeByName(const std::string & name)
	{
		if (_stricmp(name.c_str(), "HORZ") == 0)
		{
			return ImageTilingMode::Horizontally;
		}
		else if (_stricmp(name.c_str(), "VERT") == 0)
		{
			return ImageTilingMode::Vertically;
		}
		else if (_stricmp(name.c_str(), "BOTH") == 0)
		{
			return ImageTilingMode::Both;
		}

		// Default value
		return ImageTilingMode::None;
	}

	std::string ImageTilingModeName(ImageTilingMode alignment)
	{
		switch (alignment)
		{
		case ImageTilingMode::None:
			return "NONE";
		case ImageTilingMode::Horizontally:
			return "HORZ";
		case ImageTilingMode::Vertically:
			return "VERT";
		case ImageTilingMode::Both:
			return "BOTH";
		default:
			return "NONE";
		}
	}
}
