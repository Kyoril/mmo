// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "text_component.h"
#include "geometry_buffer.h"
#include "frame.h"
#include "font_mgr.h"

#include "base/utilities.h"

#include <utility>

#include "frame_mgr.h"


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

	void TextComponent::CacheText(const Rect& area)
	{
		// Remove line cache
		m_lineCache.clear();

		// Gets the text that should be displayed for this frame.
		const std::string& text = m_frame->GetVisualText();

		// Now, split the text in lines depending on formatting
		std::string::size_type pos = 0;
		std::string::size_type prev = 0;
		while ((pos = text.find('\n', prev)) != std::string::npos)
		{
			// Capture the line
			m_lineCache.push_back(text.substr(prev, pos - prev));
			prev = pos + 1;
		}

		// Append last line remaining
		m_lineCache.push_back(text.substr(prev));

		// Apply wrapping to each line and eventually split the lines into even more lines by doing so
		ApplyWrapping(area);
	}

	void TextComponent::ApplyWrapping(const Rect& area)
	{
		const float textScale = FrameManager::Get().GetUIScale().y;
		
		if (area.GetWidth() > 0.0f && !m_lineCache.empty())
		{
			const FontPtr font = m_frame->GetFont();

			// Iterate through each line, character per character
			auto lineIt = m_lineCache.begin();
			for (auto lineIt = m_lineCache.begin(); lineIt != m_lineCache.end();)
			{
				auto line = *lineIt;

				// Check if we needed to wrap and where the last word 
				bool wrapped = false;
				size_t lastWordIndex = 0;

				// Check if we need to split the line based on wrapping
				float offset = area.left;
				for (size_t i = 0; i < line.length(); ++i)
				{
					// Grab line character at the given position
					const char c = line[i];
					if (c == ' ')
					{
						lastWordIndex = i;
					}

					const auto* glyph = font->GetGlyphData(c);
					if (glyph)
					{
						offset += glyph->GetAdvance(textScale);
						if (offset > area.right)
						{
							offset = area.left;

							lineIt = m_lineCache.insert(lineIt, line.substr(0, lastWordIndex));
							lineIt = m_lineCache.erase(lineIt + 1);
							lineIt = m_lineCache.insert(lineIt, line.substr(lastWordIndex + 1));
							wrapped = true;

							break;
						}
					}
				}

				// Next line
				if (!wrapped)
				{
					++lineIt;
				}
			}
		}
	}

	void TextComponent::Render(const Rect& area, const Color& color)
	{
		const float textScale = FrameManager::Get().GetUIScale().y;

		// Grab a font pointer
		FontPtr font = m_frame->GetFont();
		if (font)
		{
			ASSERT(m_frame);

			// Get frame area rect of this component
			const Rect frameRect = GetArea(area);

			// TODO: Right now, we get the area passed in the render function. This requires us to
			// invalidate (or check) the cache every time the component is rendered! It would be better
			// to have the area as a member variable and change it if needed.
			CacheText(frameRect);

			// Calculate final text position in component
			Point position = frameRect.GetPosition();

			// Apply vertical alignment formatting
			if (m_vertAlignment == VerticalAlignment::Center)
			{
				position.y += frameRect.GetHeight() * 0.5f - font->GetHeight(textScale) * 0.5f;
			}
			else if (m_vertAlignment == VerticalAlignment::Bottom)
			{
				position.y += frameRect.GetHeight() - font->GetHeight(textScale);
			}

			// Now, render each line of text, separately
			for (const auto& line : m_lineCache)
			{
				// Calculate the text width and cache it for later use
				const float width = font->GetTextWidth(line, textScale);

				// Apply horizontal alignment
				position.x = frameRect.GetPosition().x;
				if (m_horzAlignment == HorizontalAlignment::Center)
				{
					position.x += frameRect.GetWidth() * 0.5f - width * 0.5f;
				}
				else if (m_horzAlignment == HorizontalAlignment::Right)
				{
					position.x += frameRect.GetWidth() - width;
				}

				// Apply color multiplication
				Color c = color;
				c *= m_color;

				// Determine the position to render the font at
				font->DrawText(line, position, m_frame->GetGeometryBuffer(), textScale, c);
				position.y += font->GetHeight(textScale);
			}
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
