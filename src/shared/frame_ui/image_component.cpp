// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "image_component.h"
#include "geometry_buffer.h"
#include "geometry_helper.h"
#include "frame.h"
#include "frame_mgr.h"
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
		copy->m_imagePropertyName = m_imagePropertyName;
		copy->m_tintPropertyName = m_tintPropertyName;
		copy->m_width = m_width;
		copy->m_height = m_height;

		return copy;
	}

	void ImageComponent::OnFrameChanged()
	{
		FrameComponent::OnFrameChanged();

		SetImagePropertyName(m_imagePropertyName);
		SetTintPropertyName(m_tintPropertyName);
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
		m_imagePropertyConnection.disconnect();

		m_imagePropertyName = std::move(propertyName);
		if (m_imagePropertyName.empty())
		{
			return;
		}

		auto* observedProperty = m_frame->GetProperty(m_imagePropertyName);
		if (observedProperty == nullptr)
		{
			WLOG("Unknown property name for frame " << m_frame->GetName() << ": " << m_imagePropertyName);
			return;
		}

		m_imagePropertyConnection = observedProperty->Changed += [&](const Property& changedProperty)
		{
			SetImageFile(changedProperty.GetValue());
		};

		SetImageFile(observedProperty->GetValue());
	}

	void ImageComponent::SetTintPropertyName(std::string propertyName)
	{
		m_tintPropertyConnection.disconnect();

		m_tintPropertyName = std::move(propertyName);
		if (m_tintPropertyName.empty())
		{
			return;
		}

		auto* observedProperty = m_frame->GetProperty(m_tintPropertyName);
		if (observedProperty == nullptr)
		{
			WLOG("Unknown property name for frame " << m_frame->GetName() << ": " << m_tintPropertyName);
			return;
		}

		auto handler = [&](const Property& changedProperty)
			{
				argb_t argb;

				std::stringstream colorStream;
				colorStream.str(changedProperty.GetValue());
				colorStream.clear();
				colorStream >> std::hex >> argb;
				SetTint(Color(argb));

				m_frame->Invalidate(false);
			};

		m_tintPropertyConnection = observedProperty->Changed += handler;

		// Trigger handler to initialize the property value
		handler(*observedProperty);
	}

	void ImageComponent::SetSize(uint16 width, uint16 height)
	{
		m_width = width;
		m_height = height;

		if (m_frame)
		{
			m_frame->Invalidate();
		}
	}

	Size ImageComponent::GetSize() const
	{
		uint16 realWidth = m_width;
		if (realWidth == 0) realWidth = m_texture->GetWidth();

		uint16 realHeight = m_height;
		if (realHeight == 0) realHeight = m_texture->GetHeight();

		return Size(realWidth, realHeight);
	}

	Rect ImageComponent::GetArea(const Rect& area) const
	{
		Rect baseArea = FrameComponent::GetArea(area);

		if (m_width != 0 || m_height != 0)
		{
			const auto scale = FrameManager::Get().GetUIScale();

			const auto size = GetSize();
			baseArea.SetSize(Size(size.width * scale.x, size.height * scale.y));
		}
		
		return baseArea;
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
