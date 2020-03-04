// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_text_component.h"
#include "geometry_buffer.h"
#include "frame.h"



namespace mmo
{
	TextComponent::TextComponent(const std::string & fontFile, float fontSize, float outline)
		: FrameComponent()
	{
		m_font = std::make_shared<Font>();
		VERIFY(m_font->Initialize(fontFile, fontSize, outline));
	}

	void TextComponent::Render(Frame& frame) const
	{
		if (m_font)
		{
			// Calculate the text width and cache it for later use
			const float width = m_font->GetTextWidth(frame.GetText());
			m_font->DrawText(frame.GetText(), Point::Zero, frame.GetGeometryBuffer());
		}
	}
}
