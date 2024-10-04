// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "image_component.h"
#include "geometry_buffer.h"
#include "geometry_helper.h"
#include "frame.h"
#include "base/macros.h"
#include "graphics/texture_mgr.h"


namespace mmo
{
	ImageComponent::ImageComponent(Frame& frame, const std::string& filename)
		: FrameComponent(frame)
		, m_width(0)
		, m_height(0)
		, m_tiling(ImageTilingMode::None)
	{
		SetImageFile(filename);
	}

	std::unique_ptr<FrameComponent> ImageComponent::Copy() const
	{
		ASSERT(m_frame);

		auto copy = std::make_unique<ImageComponent>(*m_frame, "");
		
		CopyBaseAttributes(*copy);

		copy->m_tiling = m_tiling;
		copy->m_tint = m_tint;
		copy->m_filename = m_filename;
		copy->m_texture = m_texture;
		copy->m_propertyName = m_propertyName;

		return copy;
	}

	void ImageComponent::OnFrameChanged()
	{
		FrameComponent::OnFrameChanged();

		SetImagePropertyName(m_propertyName);
	}

	void ImageComponent::Render(const Rect& area, const Color& color)
	{
		if (!m_texture)
		{
			return;
		}

		// Bind the texture object
		ASSERT(m_frame);
		m_frame->GetGeometryBuffer().SetActiveTexture(m_texture);
		
		// Calculate final color
		Color finalColor = m_tint;
		finalColor *= color;

		const Rect frameRect = GetArea(area);

		// Default source rect encapsulates the whole image area
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
			srcRect.SetHeight(factorY * srcRect.GetHeight());
		}

		// Create the rectangle
		GeometryHelper::CreateRect(m_frame->GetGeometryBuffer(),
			finalColor,
			frameRect,
			srcRect,
			m_texture->GetWidth(), m_texture->GetHeight());
	}

	void ImageComponent::SetTilingMode(ImageTilingMode mode)
	{
		m_tiling = mode;

		if (m_texture)
		{
			m_texture->SetTextureAddressModeW(TextureAddressMode::Clamp);
			if (m_tiling == ImageTilingMode::Horizontally || m_tiling == ImageTilingMode::Both)
			{
				m_texture->SetTextureAddressModeU(TextureAddressMode::Wrap);
			}
			if (m_tiling == ImageTilingMode::Vertically || m_tiling == ImageTilingMode::Both)
			{
				m_texture->SetTextureAddressModeV(TextureAddressMode::Wrap);
			}
			if (m_tiling == ImageTilingMode::None)
			{
				m_texture->SetTextureAddressModeU(TextureAddressMode::Clamp);
				m_texture->SetTextureAddressModeV(TextureAddressMode::Clamp);
			}
		}
	}

	void ImageComponent::SetTint(argb_t tint)
	{
		m_tint = tint;
	}

	void ImageComponent::SetImageFile(const std::string& filename)
	{
		m_texture.reset();

		m_filename = filename;
		if (!m_filename.empty())
		{
			m_texture = TextureManager::Get().CreateOrRetrieve(m_filename);
			if (m_texture)
			{
				m_texture->SetTextureAddressModeW(TextureAddressMode::Clamp);
				if (m_tiling == ImageTilingMode::Horizontally || m_tiling == ImageTilingMode::Both)
				{
					m_texture->SetTextureAddressModeU(TextureAddressMode::Wrap);
				}
				if (m_tiling == ImageTilingMode::Vertically || m_tiling == ImageTilingMode::Both)
				{
					m_texture->SetTextureAddressModeV(TextureAddressMode::Wrap);
				}
				if (m_tiling == ImageTilingMode::None)
				{
					m_texture->SetTextureAddressModeU(TextureAddressMode::Clamp);
					m_texture->SetTextureAddressModeV(TextureAddressMode::Clamp);
				}
			}
		}

		if (m_frame)
		{
			m_frame->Invalidate(false);
		}
	}

	void ImageComponent::SetImagePropertyName(std::string propertyName)
	{
		m_propertyConnection.disconnect();

		m_propertyName = std::move(propertyName);
		if (m_propertyName.empty())
		{
			return;
		}

		auto* observedProperty = m_frame->GetProperty(m_propertyName);
		if (observedProperty == nullptr)
		{
			WLOG("Unknown property name for frame " << m_frame->GetName() << ": " << m_propertyName);
			return;
		}

		m_propertyConnection += observedProperty->Changed += [&](const Property& changedProperty)
		{
			SetImageFile(changedProperty.GetValue());
		};
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
