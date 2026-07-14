// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "text_component.h"
#include "geometry_buffer.h"
#include "frame.h"
#include "font_mgr.h"
#include "hyperlink.h"
#include "utf8_utils.h"

#include "base/utilities.h"

#include <utility>

#include "frame_mgr.h"


namespace mmo
{
	TextComponent::TextComponent(Frame& frame)
		: FrameComponent(frame)
	{
		// Connect hyperlink clicked signal to frame event system
		HyperlinkClicked.connect([this](const std::string& type, const std::string& payload)
		{
			// Trigger a generic hyperlink event that can be handled in Lua
			m_frame->TriggerEvent("HYPERLINK_CLICKED", type, payload);
		});

		// Connect to frame's mouse down event to handle hyperlink clicks
		m_frame->MouseDown.connect([this](const MouseEventArgs& args)
		{
			OnMouseClick(Point(static_cast<float>(args.GetX()), static_cast<float>(args.GetY())));
		});
	}

	std::unique_ptr<FrameComponent> TextComponent::Copy() const
	{
		ASSERT(m_frame);

		auto copy = std::make_unique<TextComponent>(*m_frame);
		CopyBaseAttributes(*copy);
		copy->SetHorizontalAlignment(m_horzAlignment);
		copy->SetVerticalAlignment(m_vertAlignment);
		copy->SetColor(m_color);
		copy->SetWordWrap(m_wordWrap);
		copy->SetHorzAlignmentPropertyName(m_horzAlignPropertyName);
		copy->SetVertAlignmentPropertyName(m_vertAlignPropertyName);
		copy->SetColorPropertyName(m_colorPropertyName);
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

	void TextComponent::SetWordWrap(const bool wordWrap)
	{
		if (m_wordWrap != wordWrap)
		{
			m_wordWrap = wordWrap;
			m_cacheValid = false;
		}
	}

	void TextComponent::SetHorzAlignmentPropertyName(std::string propertyName)
	{
		m_horzAlignPropertyConnection.disconnect();

		m_horzAlignPropertyName = std::move(propertyName);
		if (m_horzAlignPropertyName.empty())
		{
			return;
		}

		auto* observedProperty = m_frame->GetProperty(m_horzAlignPropertyName);
		if (observedProperty == nullptr)
		{
			WLOG("Unknown property name for frame " << m_frame->GetName() << ": " << m_horzAlignPropertyName);
			return;
		}

		auto handler = [&](const Property& changedProperty)
			{
				if (changedProperty.GetValue() == "LEFT")
				{
					SetHorizontalAlignment(HorizontalAlignment::Left);
				}
				else if (changedProperty.GetValue() == "CENTER")
				{
					SetHorizontalAlignment(HorizontalAlignment::Center);
				}
				else if (changedProperty.GetValue() == "RIGHT")
				{
					SetHorizontalAlignment(HorizontalAlignment::Right);
				}

				m_frame->Invalidate();
			};

		m_horzAlignPropertyConnection = observedProperty->Changed += handler;

		// Trigger handler to initialize the property value
		handler(*observedProperty);
	}

	void TextComponent::SetVertAlignmentPropertyName(std::string propertyName)
	{
		m_vertAlignPropertyConnection.disconnect();

		m_vertAlignPropertyName = std::move(propertyName);
		if (m_vertAlignPropertyName.empty())
		{
			return;
		}

		auto* observedProperty = m_frame->GetProperty(m_vertAlignPropertyName);
		if (observedProperty == nullptr)
		{
			WLOG("Unknown property name for frame " << m_frame->GetName() << ": " << m_vertAlignPropertyName);
			return;
		}

		auto handler = [&](const Property& changedProperty)
			{
				if (changedProperty.GetValue() == "TOP")
				{
					SetVerticalAlignment(VerticalAlignment::Top);
				}
				else if (changedProperty.GetValue() == "CENTER")
				{
					SetVerticalAlignment(VerticalAlignment::Center);
				}
				else if (changedProperty.GetValue() == "BOTTOM")
				{
					SetVerticalAlignment(VerticalAlignment::Bottom);
				}

				m_frame->Invalidate();
			};

		m_vertAlignPropertyConnection = observedProperty->Changed += handler;

		// Trigger handler to initialize the property value
		handler(*observedProperty);
	}

	void TextComponent::SetColorPropertyName(std::string propertyName)
	{
		m_colorPropertyConnection.disconnect();

		m_colorPropertyName = std::move(propertyName);
		if (m_colorPropertyName.empty())
		{
			return;
		}

		auto* observedProperty = m_frame->GetProperty(m_colorPropertyName);
		if (observedProperty == nullptr)
		{
			WLOG("Unknown property name for frame " << m_frame->GetName() << ": " << m_colorPropertyName);
			return;
		}

		auto handler = [&](const Property& changedProperty)
			{
				argb_t argb;

				std::stringstream colorStream;
				colorStream.str(changedProperty.GetValue());
				colorStream.clear();
				colorStream >> std::hex >> argb;
				SetColor(Color(argb));

				m_frame->Invalidate(false);
			};

		m_colorPropertyConnection = observedProperty->Changed += handler;

		// Trigger handler to initialize the property value
		handler(*observedProperty);
	}

	void TextComponent::OnFrameChanged()
	{
		FrameComponent::OnFrameChanged();

		// Refresh property values
		SetColorPropertyName(m_colorPropertyName);
		SetHorzAlignmentPropertyName(m_horzAlignPropertyName);
		SetVertAlignmentPropertyName(m_vertAlignPropertyName);
	}

	void TextComponent::CacheText(const Rect& area)
	{
		// Gets the text that should be displayed for this frame.
		const std::string& text = m_frame->GetVisualText();

		// Skip the (comparatively expensive) markup parse + wrapping rebuild when none of its
		// inputs changed. Render() calls this on every redraw, and most redraws are triggered by
		// something other than the text itself (hover states, animations, child invalidations).
		const FontPtr font = m_frame->GetFont();
		const float textScale = FrameManager::Get().GetTextScale();
		const argb_t colorArgb = m_color.GetARGB();

		if (m_cacheValid &&
			area == m_cachedArea &&
			colorArgb == m_cachedColorArgb &&
			font.get() == m_cachedFont &&
			textScale == m_cachedScale &&
			text == m_cachedTextValue)
		{
			return;
		}

		// Remove line cache
		m_lineCache.clear();

		// Parse the text for hyperlinks and color formatting
		m_parsedText = ParseTextMarkup(text, m_color);

		// Now, split the parsed plain text in lines depending on formatting
		const std::string& plainText = m_parsedText.plainText;
		std::string::size_type pos = 0;
		std::string::size_type prev = 0;
		while ((pos = plainText.find('\n', prev)) != std::string::npos)
		{
			// Capture the line
			m_lineCache.push_back(plainText.substr(prev, pos - prev));
			prev = pos + 1;
		}

		// Append last line remaining
		m_lineCache.push_back(plainText.substr(prev));

		// Apply wrapping to each line and eventually split the lines into even more lines by doing so
		ApplyWrapping(area);

		// Remember the inputs this cache was built from.
		m_cachedTextValue = text;
		m_cachedArea = area;
		m_cachedColorArgb = colorArgb;
		m_cachedFont = font.get();
		m_cachedScale = textScale;
		m_cacheValid = true;
	}

	void TextComponent::ApplyWrapping(const Rect& area)
	{
		if (!m_wordWrap)
		{
			return;
		}

		const float textScale = FrameManager::Get().GetTextScale();

		if (area.GetWidth() > 0.0f && !m_lineCache.empty())
		{
			const FontPtr font = m_frame->GetFont();

			// Iterate through each line, codepoint per codepoint
			for (auto lineIt = m_lineCache.begin(); lineIt != m_lineCache.end();)
			{
				auto line = *lineIt;

				// Check if we needed to wrap and where the last word
				bool wrapped = false;
				size_t lastWordIndex = 0;

				// Check if we need to split the line based on wrapping
				float offset = area.left;
				for (size_t byteIndex = 0; byteIndex < line.length();)
				{
					// Decode the next UTF-8 codepoint. Multi-byte characters (like german
					// umlauts) must be measured as one glyph, not one glyph per byte,
					// otherwise the wrap points disagree with Font::GetLineCount and the
					// text overflows its frame.
					const size_t charStart = byteIndex;
					const uint32 codepoint = utf8::next_codepoint(line, byteIndex);
					if (codepoint == 0)
					{
						// Undecodable byte: next_codepoint already advanced past it
						continue;
					}

					if (codepoint == ' ')
					{
						lastWordIndex = charStart;
					}

					const auto* glyph = font->GetGlyphData(codepoint);
					if (glyph)
					{
						offset += glyph->GetAdvance(textScale);
						if (offset > area.right)
						{
							// Split at the last space. If the line has no space so far,
							// break mid-word before the current character, but keep at
							// least one character on the line to guarantee progress.
							size_t splitIndex = lastWordIndex;
							size_t restIndex = lastWordIndex + 1;
							if (lastWordIndex == 0)
							{
								splitIndex = (charStart > 0) ? charStart : byteIndex;
								restIndex = splitIndex;
							}

							lineIt = m_lineCache.insert(lineIt, line.substr(0, splitIndex));
							lineIt = m_lineCache.erase(lineIt + 1);
							lineIt = m_lineCache.insert(lineIt, line.substr(restIndex));
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
		const float textScale = FrameManager::Get().GetTextScale();

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

			// Apply color multiplication
			Color c = color;
			c *= m_color;

			// Route through the color/hyperlink-aware path when the text carries inline markup.
			// RenderTraditional draws the stripped plain text and ignores colorChanges, so inline
			// |cAARRGGBB..|r color codes only take effect here (hyperlinks or standalone color runs).
			if (!m_parsedText.hyperlinks.empty() || !m_parsedText.colorChanges.empty())
			{
				// Has hyperlinks and/or inline color runs - use the markup-aware renderer.
				RenderWithHyperlinks(frameRect, c, textScale);
			}
			else
			{
				// Plain text - use traditional rendering with full alignment and word-wrap support.
				RenderTraditional(frameRect, c, textScale);
			}
		}
	}

	void TextComponent::RenderTraditional(const Rect& area, const Color& color, float textScale)
	{
		FontPtr font = m_frame->GetFont();
		if (!font)
			return;

		// Calculate the total height of all lines
		const float lineHeight = font->GetHeight(textScale);
		const float totalTextHeight = lineHeight * m_lineCache.size();

		// Calculate vertical offset based on alignment
		float yOffset = 0.0f;
		if (m_vertAlignment == VerticalAlignment::Center)
		{
			yOffset = (area.GetHeight() - totalTextHeight) * 0.5f;
		}
		else if (m_vertAlignment == VerticalAlignment::Bottom)
		{
			yOffset = area.GetHeight() - totalTextHeight;
		}

		// Render each line with proper alignment
		for (size_t lineIndex = 0; lineIndex < m_lineCache.size(); ++lineIndex)
		{
			const auto& line = m_lineCache[lineIndex];
			if (line.empty())
				continue;

			// Calculate horizontal position based on alignment
			float xOffset = 0.0f;
			if (m_horzAlignment == HorizontalAlignment::Center)
			{
				const float lineWidth = font->GetTextWidth(line, textScale);
				xOffset = (area.GetWidth() - lineWidth) * 0.5f;
			}
			else if (m_horzAlignment == HorizontalAlignment::Right)
			{
				const float lineWidth = font->GetTextWidth(line, textScale);
				xOffset = area.GetWidth() - lineWidth;
			}

			// Calculate final position for this line
			Point linePosition(
				area.left + xOffset,
				area.top + yOffset + lineIndex * lineHeight
			);

			// Render the line
			font->DrawText(line, linePosition, m_frame->GetGeometryBuffer(), textScale, color.GetARGB());
		}
	}

	void TextComponent::RenderWithHyperlinks(const Rect& area, const Color& color, float textScale)
	{
		FontPtr font = m_frame->GetFont();
		if (!font)
			return;

		// For now, hyperlink rendering only supports simple alignment
		// This is a limitation but ensures hyperlinks work correctly
		Point position = area.GetPosition();

		// Apply simple vertical alignment (top/center/bottom)
		if (m_vertAlignment == VerticalAlignment::Center)
		{
			position.y += area.GetHeight() * 0.5f - font->GetHeight(textScale) * 0.5f;
		}
		else if (m_vertAlignment == VerticalAlignment::Bottom)
		{
			position.y += area.GetHeight() - font->GetHeight(textScale);
		}

		// Apply simple horizontal alignment for the entire text block
		if (m_horzAlignment == HorizontalAlignment::Center)
		{
			const float textWidth = font->GetTextWidth(m_parsedText.plainText, textScale);
			position.x += (area.GetWidth() - textWidth) * 0.5f;
		}
		else if (m_horzAlignment == HorizontalAlignment::Right)
		{
			const float textWidth = font->GetTextWidth(m_parsedText.plainText, textScale);
			position.x += area.GetWidth() - textWidth;
		}

		// Use hyperlink-aware text rendering
		font->DrawTextWithHyperlinks(m_parsedText, position, m_frame->GetGeometryBuffer(), textScale, color.GetARGB());
	}

	void TextComponent::OnMouseClick(const Point& position)
	{
		// Check if any hyperlink was clicked
		for (const auto& hyperlink : m_parsedText.hyperlinks)
		{
			if (hyperlink.bounds.IsPointInRect(position))
			{
				// Fire the hyperlink clicked signal
				HyperlinkClicked(hyperlink.type, hyperlink.payload);
				break;
			}
		}
	}

	VerticalAlignment VerticalAlignmentByName(const std::string& name)
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
