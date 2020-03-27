// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "text_component.h"
#include "geometry_buffer.h"
#include "frame.h"
#include "font_mgr.h"

#include "base/utilities.h"

#include <utility>


namespace mmo
{
	TextComponent::TextComponent(Frame& frame)
		: FrameComponent(frame)
	{
	}

	std::unique_ptr<FrameComponent> TextComponent::Copy() const
	{
		ASSERT(m_frame);

		auto copy = std::make_unique<TextComponent>(*m_frame);
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

	void TextComponent::Render(const Rect& area, const Color& color) const
	{
		FontPtr font = m_frame->GetFont();
		if (font)
		{
			ASSERT(m_frame);

			const Rect frameRect = GetArea(area);

			// Gets the text that should be displayed for this frame.
			const std::string& text = m_frame->GetVisualText();
	
			// Calculate the text width and cache it for later use
			const float width = font->GetTextWidth(text);

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
				position.y += frameRect.GetHeight() * 0.5f - font->GetHeight() * 0.5f;
			}
			else if (m_vertAlignment == VerticalAlignment::Bottom)
			{
				position.y += frameRect.GetHeight() - font->GetHeight();
			}

			// Apply color multiplication
			Color c = color;
			c *= m_color;

			// Determine the position to render the font at
			font->DrawText(text, position, m_frame->GetGeometryBuffer(), 1.0f, c);
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
