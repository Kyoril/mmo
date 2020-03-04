// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_text_component.h"
#include "geometry_buffer.h"
#include "frame.h"



namespace mmo
{
	TextComponent::TextComponent(Frame& frame, const std::string & fontFile, float fontSize, float outline)
		: FrameComponent(frame)
		, m_width(0.0f)
	{
		m_font = std::make_shared<Font>();
		VERIFY(m_font->Initialize(fontFile, fontSize, outline));

		m_frameConnection = m_frame.TextChanged.connect(*this, &TextComponent::OnTextChanged);
		OnTextChanged();
	}

	void TextComponent::Render(GeometryBuffer & buffer) const
	{
		m_font->DrawText(m_frame.GetText(), Point::Zero, buffer);
	}

	void TextComponent::OnTextChanged()
	{
		ASSERT(m_font);

		if (m_frame.GetText().size() == 0)
		{
			m_width = 0.0f;
			return;
		}

		// Calculate the text width and cache it for later use
		m_width = m_font->GetTextWidth(m_frame.GetText());
	}

	Size TextComponent::GetSize() const
	{
		return Size(m_width, m_font->GetHeight());
	}
}
