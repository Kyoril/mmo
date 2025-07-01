// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "textfield.h"

#include "frame_mgr.h"
#include "inline_color.h"
#include "text_component.h"
#include "utf8_utils.h"
#include "hyperlink.h"

#include "log/default_log_levels.h"


namespace mmo
{
	TextField::TextField(const std::string& type, const std::string& name)
		: Frame(type, name)
		, m_masked(false)
		, m_maskCodePoint('*')
		, m_maskTextDirty(true)
		, m_cursor(0)
		, m_horzAlign(HorizontalAlignment::Left)
		, m_vertAlign(VerticalAlignment::Center)
		, m_enabledColor(1.0f, 1.0f, 1.0f)
		, m_disabledColor(0.5f, 0.5f, 0.5f)
		, m_textAreaOffset(Point(10.0f, 10.0f), Size())
		, m_acceptsTab(false)
		, m_scrollOffset(0.0f)
		, m_parsedTextDirty(true)
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
		otherTextField->m_scrollOffset = m_scrollOffset;
		otherTextField->m_parsedTextDirty = true;
	}

	void TextField::SetTextMasked(bool value)
	{
		if (m_masked != value)
		{
			m_masked = value;
			m_needsRedraw = true;
		}
	}

	void TextField::SetText(std::string text)
	{
		// Call base implementation first
		Frame::SetText(std::move(text));
		
		// Reset scroll offset when text is set
		m_scrollOffset = 0.0f;
		
		// Mark for reparsing first
		m_parsedTextDirty = true;
		
		// Set cursor to the end of the new text
		UpdateParsedText();
		const std::size_t textLength = m_masked ? utf8::length(GetVisualText()) : utf8::length(m_parsedText.plainText);
		m_cursor = static_cast<int32>(textLength);
		
		// Ensure cursor is visible
		EnsureCursorVisible();
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

		// Make sure we have up-to-date parsed text
		const_cast<TextField*>(this)->UpdateParsedText();

		float offset = 0.0f;
		
		// Use the plain text for cursor calculations when not masked
		const std::string& textForCalculation = m_masked ? GetVisualText() : m_parsedText.plainText;
		
		// Process each character up to the caret position
		std::size_t charIndex = 0;
		
		for (std::size_t byteIndex = 0; byteIndex < textForCalculation.length() && charIndex < static_cast<std::size_t>(m_cursor);)
		{
			std::size_t iterations = 1;
			uint32_t codepoint;
			
			// Handle special ASCII control characters
			if (byteIndex < textForCalculation.length() && textForCalculation[byteIndex] == '\t')
			{
				codepoint = ' ';
				iterations = 4;
				byteIndex++;
			}
			else
			{
				// Process UTF-8 character
				size_t startPos = byteIndex;
				codepoint = utf8::next_codepoint(textForCalculation, byteIndex);
				
				// If we couldn't decode a valid codepoint, skip this byte
				if (codepoint == 0 && startPos < byteIndex)
				{
					continue;
				}
			}

			if (const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint))
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

		// Update parsed text
		UpdateParsedText();

		// Use the plain text for cursor calculations when not masked
		const std::string& textForCalculation = m_masked ? GetVisualText() : m_parsedText.plainText;

		// Adjust position for scroll offset
		const float adjustedX = position.x;

		// Check for out of bounds
		if (adjustedX <= m_textAreaOffset.left)
		{
			return 0;
		}
		if (adjustedX >= GetAbsoluteFrameRect().GetWidth() - m_textAreaOffset.right)
		{
			return utf8::length(textForCalculation);
		}

		// Now iterate the string value
		float x = m_textAreaOffset.left;
		
		// Character index (not byte index)
		int32 charIndex = 0;
		
		// Iterate through UTF-8 string
		for (std::size_t byteIndex = 0; byteIndex < textForCalculation.length();)
		{
			uint32_t codepoint;
			
			// Handle tab character separately
			if (textForCalculation[byteIndex] == '\t')
			{
				codepoint = ' '; // Use space as substitute for width calculation
				byteIndex++;
				
				// Get glyph information
				const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint);
				if (glyph != nullptr)
				{                
					// Calculate width with tab's 4x space
					const float advance = glyph->GetAdvance(textScale) * 4;
					if (x + advance > adjustedX)
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
				codepoint = utf8::next_codepoint(textForCalculation, byteIndex);
				
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
					if (x + advance > adjustedX)
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

		// Make sure we have up-to-date parsed text
		const_cast<TextField*>(this)->UpdateParsedText();

		float x = m_textAreaOffset.left * textScale;
		
		// Use the plain text for cursor calculations when not masked
		const std::string& textForCalculation = m_masked ? GetVisualText() : m_parsedText.plainText;
		
		// Process each character up to the caret position
		std::size_t charIndex = 0;
		
		for (std::size_t byteIndex = 0; byteIndex < textForCalculation.length() && charIndex < static_cast<std::size_t>(m_cursor);)
		{
			std::size_t iterations = 1;
			uint32_t codepoint;
			
			// Handle special ASCII control characters
			if (byteIndex < textForCalculation.length() && textForCalculation[byteIndex] == '\t')
			{
				codepoint = ' ';
				iterations = 4;
				byteIndex++;
			}
			else
			{
				// Process UTF-8 character
				size_t startPos = byteIndex;
				codepoint = utf8::next_codepoint(textForCalculation, byteIndex);
				
				// If we couldn't decode a valid codepoint, skip this byte
				if (codepoint == 0 && startPos < byteIndex)
				{
					continue;
				}
			}

			if (const FontGlyph* glyph = font->GetGlyphData(m_masked ? m_maskCodePoint : codepoint))
			{
				x += glyph->GetAdvance(textScale) * iterations;
			}
			
			// Increment character index (one Unicode codepoint = one character)
			charIndex++;
		}

		return x;
	}

	void TextField::OnMouseDown(MouseButton button, int32 buttons, const Point & position)
	{
		// If the left mouse button is pressed...
		if (button == MouseButton::Left)
		{
			// Convert position to local position
			Point localPosition = position - GetAbsoluteFrameRect().GetPosition();
			
			// Adjust for scroll offset
			localPosition.x += m_scrollOffset;
			
			// Try to find cursor position
			m_cursor = GetCursorAt(localPosition);
			
			// Check if we clicked on a hyperlink
			int hyperlinkIndex = FindHyperlinkAtPosition(m_cursor);
			if (hyperlinkIndex >= 0)
			{
				// Don't place cursor inside hyperlink, trigger hyperlink event instead
				UpdateParsedText();
				const auto& hyperlink = m_parsedText.hyperlinks[hyperlinkIndex];
				TriggerEvent("HYPERLINK_CLICKED", this, hyperlink.type, hyperlink.payload);
				
				// Move cursor to after the hyperlink (plainTextEnd is already the position after)
				m_cursor = static_cast<int32>(hyperlink.plainTextEnd);
			}

			// Ensure cursor is visible
			EnsureCursorVisible();

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
			if (m_text.empty() || m_cursor <= 0)
				return;

			// Update parsed text first to ensure consistency
			UpdateParsedText();

			// Check if we're deleting a hyperlink (cursor - 1 is inside a hyperlink)
			int hyperlinkIndex = FindHyperlinkAtPosition(m_cursor - 1);
			if (hyperlinkIndex >= 0)
			{
				DeleteHyperlink(hyperlinkIndex);
				EnsureCursorVisible();
				return;
			}

			// Handle deletion when hyperlinks are present
			if (!m_parsedText.hyperlinks.empty())
			{
				// Validate cursor position first
				const std::size_t plainTextLength = utf8::length(m_parsedText.plainText);
				if (m_cursor > static_cast<int32>(plainTextLength))
				{
					// Cursor is beyond text, move to end
					m_cursor = static_cast<int32>(plainTextLength);
				}
				
				if (m_cursor > 0)
				{
					// Use rebuild approach for complex hyperlink cases
					std::size_t targetPlainLength = m_cursor - 1;
					std::string newText = RebuildTextForPlainLength(targetPlainLength);
					m_text = newText;
					
					// Update cursor
					m_cursor--;
					
					// Ensure cursor is within bounds after rebuild
					m_parsedTextDirty = true;
					UpdateParsedText();
					const std::size_t newPlainTextLength = utf8::length(m_parsedText.plainText);
					if (m_cursor > static_cast<int32>(newPlainTextLength))
					{
						m_cursor = static_cast<int32>(newPlainTextLength);
					}
				}
			}
			else
			{
				// No hyperlinks - simple case, work directly with raw text
				if (m_cursor > 0)
				{
					std::size_t bytePos = utf8::byte_index(m_text, m_cursor);
					std::size_t prevCharBytePos = bytePos;
					
					// Find the start byte of the previous character
					do {
						prevCharBytePos--;
					} while (prevCharBytePos > 0 && (m_text[prevCharBytePos] & 0xC0) == 0x80);
					
					// Delete the character from the raw text
					m_text.erase(prevCharBytePos, bytePos - prevCharBytePos);
					
					// Update cursor
					m_cursor--;
				}
			}

			OnTextChanged();
			EnsureCursorVisible();

			m_needsLayout = true;
			m_needsRedraw = true;
		}
		else if (key == 0x25)   // VK_LEFT
		{
			if (m_cursor > 0)
			{
				// Check if we're moving into a hyperlink
				int hyperlinkIndex = FindHyperlinkAtPosition(m_cursor - 1);
				if (hyperlinkIndex >= 0)
				{
					// Jump to the start of the hyperlink
					UpdateParsedText();
					m_cursor = static_cast<int32>(m_parsedText.hyperlinks[hyperlinkIndex].plainTextStart);
				}
				else
				{
					m_cursor--;
				}
				EnsureCursorVisible();
				m_needsRedraw = true;
			}
		}
		else if (key == 0x27)   // VK_RIGHT
		{
			UpdateParsedText();
			const std::size_t textLength = m_masked ? utf8::length(GetVisualText()) : utf8::length(m_parsedText.plainText);
			
			if (m_cursor < static_cast<int32>(textLength))
			{
				// Check if we're moving into a hyperlink
				int hyperlinkIndex = FindHyperlinkAtPosition(m_cursor);
				if (hyperlinkIndex >= 0)
				{
					// Jump to the end of the hyperlink
					UpdateParsedText();
					m_cursor = static_cast<int32>(m_parsedText.hyperlinks[hyperlinkIndex].plainTextEnd);
				}
				else
				{
					m_cursor++;
				}
				EnsureCursorVisible();
				m_needsRedraw = true;
			}
		}
		else if (key == 0x24)   // VK_HOME
		{
			m_cursor = 0;
			EnsureCursorVisible();
			m_needsRedraw = true;
		}
		else if (key == 0x23)   // VK_END
		{
			UpdateParsedText();
			const std::size_t textLength = m_masked ? utf8::length(GetVisualText()) : utf8::length(m_parsedText.plainText);
			m_cursor = static_cast<int32>(textLength);
			EnsureCursorVisible();
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

		// Don't insert text inside hyperlinks
		int hyperlinkIndex = FindHyperlinkAtPosition(m_cursor);
		if (hyperlinkIndex >= 0)
		{
			// Move cursor to end of hyperlink before inserting
			UpdateParsedText();
			m_cursor = static_cast<int32>(m_parsedText.hyperlinks[hyperlinkIndex].plainTextEnd);
		}

		// Convert the 16-bit codepoint to UTF-8 and insert it at the cursor position
		std::string utf8Char;
		utf8::append_codepoint(utf8Char, codepoint);
		
		// For simplicity and reliability, always append to the end when there's no markup
		// This avoids complex text rebuilding issues
		UpdateParsedText();
		
		if (m_parsedText.hyperlinks.empty())
		{
			// No hyperlinks - simple case, work directly with text
			// Compare cursor to raw text length since there are no hyperlinks
			const std::size_t rawTextLength = utf8::length(m_text);
			
			if (m_cursor >= static_cast<int32>(rawTextLength))
			{
				// Append to end
				m_text.append(utf8Char);
			}
			else
			{
				// Insert at byte position corresponding to cursor
				std::size_t bytePos = utf8::byte_index(m_text, m_cursor);
				m_text.insert(bytePos, utf8Char);
			}
		}
		else
		{
			// Complex case with hyperlinks - always append to end to avoid complex issues
			// This ensures consistent behavior and prevents cursor positioning problems
			m_text.append(utf8Char);
		}
		
		// Update cursor to be after the inserted character
		// Always increment by 1 since we inserted 1 character in plain text terms
		m_cursor++;
		
		// Update parsed text after modification
		UpdateParsedText();

		OnTextChanged();
		EnsureCursorVisible();

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
		
		// Invalidate parsed text
		m_parsedTextDirty = true;

		// Call superclass method
		Frame::OnTextChanged();
	}

	void TextField::UpdateParsedText()
	{
		if (m_parsedTextDirty)
		{
			// Parse the text for hyperlinks and colors
			m_parsedText = ParseTextMarkup(m_text, m_enabledColor.GetARGB());
			m_parsedTextDirty = false;
		}
	}

	void TextField::SetScrollOffset(float offset)
	{
		if (m_scrollOffset != offset)
		{
			m_scrollOffset = offset;
			Invalidate();
		}
	}

	float TextField::GetVisibleTextWidth() const
	{
		return const_cast<TextField*>(this)->GetAbsoluteFrameRect().GetWidth() - m_textAreaOffset.left - m_textAreaOffset.right;
	}

	const std::string& TextField::GetParsedPlainText() const
	{
		const_cast<TextField*>(this)->UpdateParsedText();
		return m_parsedText.plainText;
	}

	void TextField::EnsureCursorVisible()
	{
		const float cursorPixelPos = GetCaretPixelOffset(FrameManager::Get().GetUIScale().y);
		const float visibleWidth = GetVisibleTextWidth();
		
		// If cursor is to the left of visible area
		if (cursorPixelPos < m_scrollOffset)
		{
			m_scrollOffset = cursorPixelPos;
		}
		// If cursor is to the right of visible area
		else if (cursorPixelPos > m_scrollOffset + visibleWidth)
		{
			m_scrollOffset = cursorPixelPos - visibleWidth;
		}
		
		// Ensure scroll offset doesn't go negative
		if (m_scrollOffset < 0.0f)
		{
			m_scrollOffset = 0.0f;
		}
		
		Invalidate();
	}

	int TextField::FindHyperlinkAtPosition(std::size_t cursorPos)
	{
		UpdateParsedText();
		
		for (int i = 0; i < static_cast<int>(m_parsedText.hyperlinks.size()); ++i)
		{
			const auto& hyperlink = m_parsedText.hyperlinks[i];
			// Use < for plainTextEnd because it represents the position after the last character
			if (cursorPos >= hyperlink.plainTextStart && cursorPos < hyperlink.plainTextEnd)
			{
				return i;
			}
		}
		return -1;
	}

	void TextField::DeleteHyperlink(int hyperlinkIndex)
	{
		UpdateParsedText();
		
		if (hyperlinkIndex >= 0 && hyperlinkIndex < static_cast<int>(m_parsedText.hyperlinks.size()))
		{
			const auto& hyperlink = m_parsedText.hyperlinks[hyperlinkIndex];
			
			// Store the cursor position we want to end up at (start of where hyperlink was)
			int32 newCursorPos = static_cast<int32>(hyperlink.plainTextStart);
			
			// Remove the entire hyperlink markup from the raw text
			// Use the raw text positions (startIndex/endIndex) not the plain text positions
			std::size_t startByte = hyperlink.startIndex;
			std::size_t endByte = hyperlink.endIndex + 1; // +1 to include the closing character
			
			// Make sure we don't go out of bounds
			if (endByte > m_text.length())
			{
				endByte = m_text.length();
			}
			
			// Remove the hyperlink markup
			m_text.erase(startByte, endByte - startByte);
			
			// Update cursor position to the start of where the hyperlink was (in plain text coordinates)
			m_cursor = newCursorPos;
			
			// Mark for re-parsing and notify of text change
			m_parsedTextDirty = true;
			OnTextChanged();
			
			// Force a re-parse to ensure consistency for subsequent operations
			UpdateParsedText();
		}
	}

	void TextField::PopulateGeometryBuffer()
	{
		// TextField uses TextFieldRenderer for rendering, not this method
		// Call base implementation for any background/border rendering
		Frame::PopulateGeometryBuffer();
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

	std::size_t TextField::MapPlainToRawPosition(std::size_t plainPos)
	{
		UpdateParsedText();
		
		// If no hyperlinks, positions are the same
		if (m_parsedText.hyperlinks.empty())
		{
			return plainPos;
		}
		
		// Walk through the raw text and track how many plain text characters we've seen
		std::size_t rawPos = 0;
		std::size_t currentPlainPos = 0;
		
		// Process text character by character
		while (rawPos < m_text.length() && currentPlainPos < plainPos)
		{
			// Check if we're at the start of a hyperlink
			bool inHyperlink = false;
			for (const auto& hyperlink : m_parsedText.hyperlinks)
			{
				if (rawPos == hyperlink.startIndex)
				{
					// Skip to the end of this hyperlink in raw text
					rawPos = hyperlink.endIndex + 1;
					// Add the length of the display text to plain position
					currentPlainPos += utf8::length(hyperlink.displayText);
					inHyperlink = true;
					break;
				}
			}
			
			if (!inHyperlink)
			{
				// Regular character - advance both positions
				// Find the end of this UTF-8 character
				std::size_t charEnd = rawPos + 1;
				while (charEnd < m_text.length() && (m_text[charEnd] & 0xC0) == 0x80)
				{
					charEnd++;
				}
				
				rawPos = charEnd;
				currentPlainPos++;
			}
		}
		
		return rawPos;
	}

	std::string TextField::RebuildTextForPlainLength(std::size_t targetPlainLength)
	{
		UpdateParsedText();
		
		std::string result;
		std::size_t currentPlainPos = 0;
		std::size_t rawPos = 0;
		
		// Walk through the raw text and include characters until we reach the target plain length
		while (rawPos < m_text.length() && currentPlainPos < targetPlainLength)
		{
			// Check if we're at the start of a hyperlink
			bool foundHyperlink = false;
			for (const auto& hyperlink : m_parsedText.hyperlinks)
			{
				if (rawPos == hyperlink.startIndex)
				{
					// Include the entire hyperlink if we have room for its display text
					std::size_t hyperlinkDisplayLength = utf8::length(hyperlink.displayText);
					if (currentPlainPos + hyperlinkDisplayLength <= targetPlainLength)
					{
						// Include the entire hyperlink markup
						result.append(m_text, hyperlink.startIndex, hyperlink.endIndex - hyperlink.startIndex + 1);
						rawPos = hyperlink.endIndex + 1;
						currentPlainPos += hyperlinkDisplayLength;
					}
					else
					{
						// Not enough room for the entire hyperlink, stop here
						return result;
					}
					foundHyperlink = true;
					break;
				}
			}
			
			if (!foundHyperlink)
			{
				// Regular character - include it if we have room
				if (currentPlainPos < targetPlainLength)
				{
					// Find the end of this UTF-8 character
					std::size_t charStart = rawPos;
					std::size_t charEnd = rawPos + 1;
					while (charEnd < m_text.length() && (m_text[charEnd] & 0xC0) == 0x80)
					{
						charEnd++;
					}
					
					// Include this character
					result.append(m_text, charStart, charEnd - charStart);
					rawPos = charEnd;
					currentPlainPos++;
				}
				else
				{
					break;
				}
			}
		}
		
		return result;
	}

	std::string TextField::RebuildTextFromPlainPosition(std::size_t fromPlainPos)
	{
		UpdateParsedText();
		
		std::string result;
		std::size_t currentPlainPos = 0;
		std::size_t rawPos = 0;
		
		// Walk through the raw text until we reach the starting plain position
		while (rawPos < m_text.length() && currentPlainPos < fromPlainPos)
		{
			// Check if we're at the start of a hyperlink
			bool foundHyperlink = false;
			for (const auto& hyperlink : m_parsedText.hyperlinks)
			{
				if (rawPos == hyperlink.startIndex)
				{
					// Skip this hyperlink
					std::size_t hyperlinkDisplayLength = utf8::length(hyperlink.displayText);
					rawPos = hyperlink.endIndex + 1;
					currentPlainPos += hyperlinkDisplayLength;
					foundHyperlink = true;
					break;
				}
			}
			
			if (!foundHyperlink)
			{
				// Regular character - skip it
				std::size_t charEnd = rawPos + 1;
				while (charEnd < m_text.length() && (m_text[charEnd] & 0xC0) == 0x80)
				{
					charEnd++;
				}
				
				rawPos = charEnd;
				currentPlainPos++;
			}
		}
		
		// Now include everything from this point to the end
		if (rawPos < m_text.length())
		{
			result = m_text.substr(rawPos);
		}
		
		return result;
	}
}
