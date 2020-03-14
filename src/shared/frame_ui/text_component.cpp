// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "text_component.h"
#include "geometry_buffer.h"
#include "frame.h"
#include "font_mgr.h"

#include "base/utilities.h"

#include <utility>


namespace mmo
{
	TextComponent::TextComponent(Frame& frame, std::string fontFile, float fontSize, float outline)
		: FrameComponent(frame)
		, m_filename(std::move(fontFile))
		, m_fontSize(fontSize)
		, m_outline(outline)
	{
		m_font = FontManager::Get().CreateOrRetrieve(m_filename, m_fontSize, m_outline);
	}

	std::unique_ptr<FrameComponent> TextComponent::Copy() const
	{
		ASSERT(m_frame);

		auto copy = std::make_unique<TextComponent>(*m_frame, m_filename, m_fontSize, m_outline);
		CopyBaseAttributes(*copy);
		copy->SetHorizontalAlignment(m_horzAlignment);
		copy->SetVerticalAlignment(m_vertAlignment);
		copy->SetColor(m_color);
		return copy;
	}

	void TextComponent::SetHorizontalAlignment(HorizontalAlignment alignment)
	{
		m_horzAlignment = alignment;
	}

	void TextComponent::SetVerticalAlignment(VerticalAlignment alignment)
	{
		m_vertAlignment = alignment;
	}

	void TextComponent::SetColor(const Color & color)
	{
		m_color = color;
	}

	void TextComponent::Render() const
	{
		if (m_font)
		{
			ASSERT(m_frame);

			// Gets the text that should be displayed for this frame.
			const std::string& text = m_frame->GetVisualText();

			// Calculate the frame rectangle
			const Rect frameRect = GetArea();

			// Calculate the text width and cache it for later use
			const float width = m_font->GetTextWidth(text);

			// Calculate final text position in component
			Point position = frameRect.GetPosition();

			// Apply horizontal alignment
			if (m_horzAlignment == HorizontalAlignment::Center)
			{
				position.x += frameRect.GetWidth() * 0.5f - width * 0.5f;
			}
			else if (m_horzAlignment == HorizontalAlignment::Right)
			{
				position.x += frameRect.GetWidth() - width;
			}

			// Apply vertical alignment formatting
			if (m_vertAlignment == VerticalAlignment::Center)
			{
				position.y += frameRect.GetHeight() * 0.5f - m_font->GetHeight() * 0.5f;
			}
			else if (m_vertAlignment == VerticalAlignment::Bottom)
			{
				position.y += frameRect.GetHeight() - m_font->GetHeight();
			}

			// Determine the position to render the font at
			m_font->DrawText(text, position, m_frame->GetGeometryBuffer(), 1.0f, m_color);
		}
	}

	VerticalAlignment VerticalAlignmentByName(const std::string & name)
	{
		if (_stricmp(name.c_str(), "CENTER") == 0)
		{
			return VerticalAlignment::Center;
		}
		else if (_stricmp(name.c_str(), "BOTTOM") == 0)
		{
			return VerticalAlignment::Bottom;
		}

		// Default value
		return VerticalAlignment::Top;
	}

	std::string VerticalAlignmentName(VerticalAlignment alignment)
	{
		switch (alignment)
		{
		case VerticalAlignment::Top:
			return "TOP";
		case VerticalAlignment::Center:
			return "CENTER";
		case VerticalAlignment::Bottom:
			return "BOTTOM";
		default:
			// Default value
			return "TOP";
		}
	}

	HorizontalAlignment HorizontalAlignmentByName(const std::string & name)
	{
		if (_stricmp(name.c_str(), "CENTER") == 0)
		{
			return HorizontalAlignment::Center;
		}
		else if (_stricmp(name.c_str(), "RIGHT") == 0)
		{
			return HorizontalAlignment::Right;
		}

		// Default value
		return HorizontalAlignment::Left;
	}

	std::string HorizontalAlignmentName(HorizontalAlignment alignment)
	{
		switch (alignment)
		{
		case HorizontalAlignment::Left:
			return "LEFT";
		case HorizontalAlignment::Center:
			return "CENTER";
		case HorizontalAlignment::Right:
			return "RIGHT";
		default:
			// Default value
			return "LEFT";
		}
	}
}
