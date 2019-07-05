
#include "frame_font_string.h"
#include "geometry_buffer.h"



namespace mmo
{
	FrameFontString::FrameFontString(const std::string & fontFile, float fontSize, float outline)
		: m_width(0.0f)
	{
		m_font = std::make_shared<Font>();
		VERIFY(m_font->Initialize(fontFile, fontSize, outline));
	}

	void FrameFontString::Render(GeometryBuffer & buffer) const
	{
		m_font->DrawText(m_text, Point::Zero, buffer);
	}

	void FrameFontString::SetText(const std::string & text)
	{
		m_text = text;

		if (text.size() == 0)
		{
			m_width = 0.0f;
			return;
		}

		// Calculate the text width and cache it for later use
		m_width = m_font->GetTextWidth(m_text);
	}
}
