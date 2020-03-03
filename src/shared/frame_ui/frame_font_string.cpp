
#include "frame_font_string.h"
#include "geometry_buffer.h"



namespace mmo
{
	FrameFontString::FrameFontString(Frame& frame, const std::string & fontFile, float fontSize, float outline)
		: FrameObject(frame)
		, m_width(0.0f)
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

	Size FrameFontString::GetSize() const
	{
		return Size(m_width, m_font->GetHeight());
	}
}
