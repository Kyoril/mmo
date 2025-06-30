// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "text_component.h"
#include "geometry_buffer.h"
#include "frame.h"
#include "font_mgr.h"
#include "hyperlink.h"

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
		// Remove line cache
		m_lineCache.clear();

		// Gets the text that should be displayed for this frame.
		const std::string& text = m_frame->GetVisualText();

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

			// Now, render the parsed text with hyperlink support
			// Apply color multiplication
			Color c = color;
			c *= m_color;

			// Use hyperlink-aware text rendering for the entire parsed text
			font->DrawTextWithHyperlinks(m_parsedText, position, m_frame->GetGeometryBuffer(), textScale, c.GetARGB());
		}
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
