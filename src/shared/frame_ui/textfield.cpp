// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "textfield.h"

#include "frame_mgr.h"
#include "inline_color.h"
#include "text_component.h"
#include "utf8_utils.h"

#include "log/default_log_levels.h"


namespace mmo
{
	TextField::TextField(const std::string& type, const std::string& name)
		: Frame(type, name)
		, m_masked(false)
		, m_maskCodePoint('*')
		, m_maskTextDirty(true)
		, m_cursor(-1)
		, m_horzAlign(HorizontalAlignment::Left)
		, m_vertAlign(VerticalAlignment::Center)
		, m_enabledColor(1.0f, 1.0f, 1.0f)
		, m_disabledColor(0.5f, 0.5f, 0.5f)
		, m_textAreaOffset(Point(10.0f, 10.0f), Size())
		, m_acceptsTab(false)
	{
		// Add the masked property
		m_propConnections += AddProperty("Masked", "false").Changed.connect(this, &TextField::OnMaskedPropChanged);
		m_propConnections += AddProperty("AcceptsTab", "false").Changed.connect(this, &TextField::OnAcceptTabChanged);
		m_propConnections += AddProperty("EnabledTextColor", "FFFFFFFF").Changed.connect(this, &TextField::OnEnabledTextColorChanged);
		m_propConnections += AddProperty("DisabledTextColor", "FF808080").Changed.connect(this, &TextField::OnDisabledTextColorChanged);

		// Text fields are focusable by default
		m_focusable = true;
	}

	void TextField::Copy(Frame & other)
	{
		Frame::Copy(other);

		const auto otherTextField = dynamic_cast<TextField*>(&other);
		if (!otherTextField)
		{
			return;
		}

		otherTextField->m_textAreaOffset = m_textAreaOffset;
		otherTextField->m_cursor = m_cursor;
		otherTextField->m_masked = m_masked;
		otherTextField->m_vertAlign = m_vertAlign;
		otherTextField->m_horzAlign = m_horzAlign;
		otherTextField->m_acceptsTab = m_acceptsTab;
		otherTextField->m_enabledColor = m_enabledColor;
		otherTextField->m_disabledColor = m_disabledColor;
	}

	void TextField::SetTextMasked(bool value)
	{
		if (m_masked != value)
		{
			m_masked = value;
			m_needsRedraw = true;
		}
	}

	void TextField::SetMaskCodePoint(std::string::value_type value)
	{
		if (m_maskCodePoint != value)
		{
			m_maskCodePoint = value;
			m_maskTextDirty = true;

			if (m_masked)
			{
				m_needsRedraw = true;
			}
		}
	}

	float TextField::GetCaretPixelOffset(float uiScale) const
	{
		const FontPtr font = GetFont();
		if (!font) return 0.0f;

		float offset = 0.0f;
		argb_t dummyColor = 0;   // width calculation ignores colour

		// Process each character up to the caret position
		std::size_t byteIndex = 0;
		std::size_t charIndex = 0;
		
		while (charIndex < m_caretIndex && byteIndex < m_text.length())
		{
			if (ConsumeColourTag(m_text, byteIndex, dummyColor, dummyColor))
			{
				continue; // colour token has zero width
			}

			std::size_t iterations = 1;
			uint32_t codepoint;
			
			// Handle special ASCII control characters
			if (byteIndex < m_text.length() && m_text[byteIndex] == '\t')
			{
				codepoint = ' ';
				iterations = 4;
				byteIndex++;
			}
			else
			{
				// Process UTF-8 character
				size_t startPos = byteIndex;
				codepoint = utf8::next_codepoint(m_text, byteIndex);
				
				// If we couldn't decode a valid codepoint, skip this byte
				if (codepoint == 0 && startPos < byteIndex)
				{
					continue;
				}
			}

			if (const FontGlyph* glyph = font->GetGlyphData(codepoint))
			{
				offset += glyph->GetAdvance(uiScale) * iterations;
			}
			
			// Increment character index (one Unicode codepoint = one character)
			charIndex++;
		}

		return offset;
	}

	const std::string & TextField::GetVisualText() const
	{
		if (m_masked)
		{
			if (m_maskTextDirty)
			{
				m_maskText.clear();

				// Count the number of Unicode characters in the text
				size_t charCount = utf8::length(m_text);
				
				// Create a string with the mask character repeated for each Unicode character
				for (size_t i = 0; i < charCount; ++i)
				{
					utf8::append_codepoint(m_maskText, static_cast<uint32_t>(m_maskCodePoint));
				}

				m_maskTextDirty = false;
			}

			return m_maskText;
		}

		return Frame::GetVisualText();
	}

	int32 TextField::GetCursorAt(const Point & position)
	{
		// Get the font to use
		FontPtr font = GetFont();
		if (!font)
		{
			return -1;
		}

		const float textScale = FrameManager::Get().GetUIScale().y;

		// Check for out of bounds
		if (position.x <= m_textAreaOffset.left)
		{
			return 0;
		}
		if (position.x >= GetAbsoluteFrameRect().GetWidth() - m_textAreaOffset.right)
		{
			return utf8::length(m_text);
		}

		// Now iterate the string value
		float x = m_textAreaOffset.left;
		
		// Character index (not byte index)
		int32 charIndex = 0;
		
		// Iterate through UTF-8 string
		for (std::size_t byteIndex = 0; byteIndex < m_text.length();)
		{
			uint32_t codepoint;
			
			// Handle tab character separately
			if (m_text[byteIndex] == '\t')
			{
				codepoint = ' '; // Use space as substitute for width calculation
				byteIndex++;
				
				// Get glyph information
				const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint);
				if (glyph != nullptr)
				{                
					// Calculate width with tab's 4x space
					const float advance = glyph->GetAdvance(textScale) * 4;
					if (x + advance > position.x)
					{
						return charIndex;
					}
					// Advance position
					x += advance;
				}
			}
			else
			{
				// Process UTF-8 character
				size_t startPos = byteIndex;
				codepoint = utf8::next_codepoint(m_text, byteIndex);
				
				// Skip if invalid codepoint
				if (codepoint == 0 && startPos < byteIndex)
				{
					continue;
				}
				
				// Get glyph information
				const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint);
				if (glyph != nullptr)
				{                
					// Advance offset
					const float advance = glyph->GetAdvance(textScale);
					if (x + advance > position.x)
					{
						return charIndex;
					}
					// Advance position
					x += advance;
				}
			}

			// Increase character index
			charIndex++;
		}

		return charIndex;
	}

	void TextField::SetHorzAlignment(HorizontalAlignment value)
	{
		m_horzAlign = value;
		Invalidate();
	}

	void TextField::SetVertAlignment(VerticalAlignment value)
	{
		m_vertAlign = value;
		Invalidate();
	}

	void TextField::SetEnabledTextColor(const Color & value)
	{
		m_enabledColor = value;
		Invalidate();
	}

	void TextField::SetDisabledTextColor(const Color & value)
	{
		m_disabledColor = value;
		Invalidate();
	}

	void TextField::SetTextAreaOffset(const Rect& offset)
	{
		m_textAreaOffset = offset;
		Invalidate();
	}

	float TextField::GetCursorOffset() const
	{
		const float textScale = FrameManager::Get().GetUIScale().y;

		if (m_cursor <= 0)
		{
			return m_textAreaOffset.left * textScale;
		}
		
		// Check for font
		const auto font = GetFont();
		if (!font)
		{
			return m_textAreaOffset.left * textScale;
		}

		float x = m_textAreaOffset.left * textScale;
		
		// Character index (not byte index)
		std::size_t charIndex = 0;
		
		// Iterate through UTF-8 string
		for (std::size_t byteIndex = 0; byteIndex < m_text.length();)
		{
			if (charIndex == m_cursor)
			{
				return x;
			}
			
			// Skip color tags
			argb_t dummyColor = 0; // colour not required for counting
			if (ConsumeColourTag(m_text, byteIndex, dummyColor, dummyColor))
			{
				continue;
			}
			
			std::size_t iterations = 1;
			uint32_t codepoint;
			
			// Handle special ASCII control characters
			if (byteIndex < m_text.length() && m_text[byteIndex] == '\t')
			{
				codepoint = ' ';
				iterations = 4;
				byteIndex++;
			}
			else
			{
				// Process UTF-8 character
				size_t startPos = byteIndex;
				codepoint = utf8::next_codepoint(m_text, byteIndex);
				
				// If we couldn't decode a valid codepoint, skip this byte
				if (codepoint == 0 && startPos < byteIndex)
				{
					continue;
				}
			}

			if (const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint))
			{
				// Advance offset
				x += glyph->GetAdvance(textScale) * iterations;
			}
			
			// Increment character index
			charIndex++;
		}
		
		// If cursor is at the end of the text
		if (charIndex == m_cursor)
		{
			return x;
		}

		return x;
	}

	void TextField::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// If the left mouse button is pressed...
		if (button == MouseButton::Left)
		{
			// Convert position to local position
			const Point localPosition = position - GetAbsoluteFrameRect().GetPosition();
			
			// Try to find cursor position
			m_cursor = GetCursorAt(localPosition);

			// Redraw to display the cursor
			m_needsRedraw = true;
		}

		// Raise the signal from the super class
		Frame::OnMouseDown(button, buttons, position);
		abort_emission();
	}

	void TextField::OnMouseUp(MouseButton button, int32 buttons, const Point& position)
	{
		Frame::OnMouseUp(button, buttons, position);
		abort_emission();
	}

	void TextField::OnKeyDown(Key key)
	{
		abort_emission();

		if (!AcceptsTab() && key == 0x09)        // VK_TAB
		{
			return;
		}

		if (key == 0x0D)    // VK_RETURN
		{
			return;
		}
		
		if (key == 0x08)        // VK_BACKSPACE
		{
			if (m_text.empty())
				return;

			if (m_cursor == -1 || m_cursor >= utf8::length(m_text))
			{
				// Remove the last character (which might be multiple bytes)
				size_t lastCharStart = m_text.length();
				
				// Find the start of the last UTF-8 character
				do {
					lastCharStart--;
				} while (lastCharStart > 0 && (m_text[lastCharStart] & 0xC0) == 0x80);
				
				// Remove the last character
				m_text.erase(lastCharStart);
				m_cursor = utf8::length(m_text);
			}
			else if (m_cursor > 0)
			{
				// Convert character position to byte position
				size_t bytePos = utf8::byte_index(m_text, m_cursor);
				size_t prevCharBytePos = bytePos;
				
				// Find the start byte of the previous character
				do {
					prevCharBytePos--;
				} while (prevCharBytePos > 0 && (m_text[prevCharBytePos] & 0xC0) == 0x80);
				
				// Delete the character
				m_text.erase(prevCharBytePos, bytePos - prevCharBytePos);
				m_cursor--;
			}
			else
			{
				// Nothing to do
				return;
			}

			OnTextChanged();

			m_needsLayout = true;
			m_needsRedraw = true;
		}
	}

	void TextField::OnKeyChar(uint16 codepoint)
	{
		abort_emission();

		if (!AcceptsTab() && codepoint == 0x09)     // VK_TAB
		{
			return;
		}

		if (codepoint == 0x0D)  // VK_RETURN
		{
			return;
		}
		
		if (codepoint == 0x8)   // VK_BACKSPACE
			return;

		// Convert the 16-bit codepoint to UTF-8 and insert it at the cursor position
		std::string utf8Char;
		utf8::append_codepoint(utf8Char, codepoint);
		
		if (m_cursor == -1 || m_cursor >= utf8::length(m_text))
		{
			// If cursor is at the end or beyond, append the character
			m_text.append(utf8Char);
			m_cursor = utf8::length(m_text);
		}
		else
		{
			// Otherwise, insert at the cursor position by converting char index to byte index
			size_t byteIndex = utf8::byte_index(m_text, m_cursor);
			m_text.insert(byteIndex, utf8Char);
			m_cursor++;
		}

		OnTextChanged();

		m_needsLayout = true;
		m_needsRedraw = true;
	}

	void TextField::OnKeyUp(Key key)
	{
		Frame::OnKeyUp(key);

		abort_emission();
	}

	void TextField::OnInputCaptured()
	{
		m_needsRedraw = true;
	}

	void TextField::OnInputReleased()
	{
		m_needsRedraw = true;
	}

	void TextField::OnTextChanged()
	{
		// Invalidate masked text
		m_maskTextDirty = true;

		// Call superclass method
		Frame::OnTextChanged();
	}

	void TextField::OnMaskedPropChanged(const Property& property)
	{
		SetTextMasked(property.GetBoolValue());
	}

	void TextField::OnAcceptTabChanged(const Property& property)
	{
		SetAcceptsTab(property.GetBoolValue());
	}

	void TextField::OnEnabledTextColorChanged(const Property& property)
	{
		argb_t argb;

		std::stringstream colorStream;
		colorStream.str(property.GetValue());
		colorStream.clear();
		colorStream >> std::hex >> argb;

		m_enabledColor = argb;

		Invalidate(false);
	}

	void TextField::OnDisabledTextColorChanged(const Property& property)
	{
		argb_t argb;

		std::stringstream colorStream;
		colorStream.str(property.GetValue());
		colorStream.clear();
		colorStream >> std::hex >> argb;

		m_disabledColor = argb;

		Invalidate(false);
	}
}
