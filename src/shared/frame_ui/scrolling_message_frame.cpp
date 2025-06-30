#include "scrolling_message_frame.h"
#include "frame_mgr.h"
#include "frame_ui/inline_color.h"
#include "hyperlink.h"

#include <algorithm>

namespace mmo
{
	ScrollingMessageFrame::ScrollingMessageFrame(const std::string& type, const std::string& name)
		: Frame(type, name)
	{
		SetClippedByParent(true);
	}

	void ScrollingMessageFrame::Copy(Frame& other)
	{
		Frame::Copy(other);
	}

	void ScrollingMessageFrame::AddMessage(std::string message, float r, float g, float b)
	{
		if (m_messages.size() > 50)
		{
			m_messages.erase(m_messages.begin());
		}

        m_messages.push_back({message, r, g, b});
		OnMessagesChanged();

		ScrollToBottom();
	}

	void ScrollingMessageFrame::ScrollUp()
	{
		if (IsAtTop())
		{
			return;
		}

		m_linePosition--;
		Invalidate();
	}

	void ScrollingMessageFrame::ScrollDown()
	{
		if (IsAtBottom())
		{
			return;
		}

		m_linePosition++;
		Invalidate();
	}

	void ScrollingMessageFrame::ScrollToTop()
	{
		m_linePosition = 0;
		Invalidate();
	}

	void ScrollingMessageFrame::ScrollToBottom()
	{
		m_linePosition = std::max<int32>(0, m_lineCache.size() - m_visibleLineCount);

		Invalidate();
	}

	void ScrollingMessageFrame::Clear()
	{
		m_messages.clear();
		m_linePosition = 0;

		OnMessagesChanged();
	}

	int ScrollingMessageFrame::GetMessageCount() const
	{
		return static_cast<int>(m_messages.size());
	}

	bool ScrollingMessageFrame::IsAtTop() const
	{
		return m_linePosition <= 0;
	}

	bool ScrollingMessageFrame::IsAtBottom() const
	{
		return m_linePosition >= m_lineCache.size() - m_linePosition;
	}

	const ScrollingMessageFrame::Message& ScrollingMessageFrame::GetMessageAt(size_t index) const
	{
		ASSERT(index < m_messages.size());

		return m_messages[index];
	}

	void ScrollingMessageFrame::PopulateGeometryBuffer()
	{
		// Detect the current state
		std::string activeState = "Disabled";
		if (IsEnabled())
		{
			activeState = "Enabled";
		}

		// Find the state imagery
		const auto* imagery = GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = GetStateImageryByName("Enabled");
		}

		// If found, draw the state imagery
		if (imagery)
		{
			imagery->Render(GetAbsoluteFrameRect(), Color::White);
		}

		// Should we render text at all?
		if (m_visibleLineCount <= 0)
		{
			return;
		}

		const float textScale = FrameManager::Get().GetUIScale().y;

		// Get the frame rectangle
		Rect frameRect = GetAbsoluteFrameRect();

		int lineCount = 0;

		// Retrieve the font
		const FontPtr font = GetFont();
		const float lineHeight = font->GetHeight(textScale);
		if (font)
		{
			for (int i = m_linePosition; i < m_lineCache.size(); ++i)
			{
				const auto& line = m_lineCache[i];
				const int linesRendered = RenderLine(line, frameRect);

				lineCount += linesRendered;
				if (lineCount > m_visibleLineCount)
				{
					break;
				}
			}
		}
	}

	int ScrollingMessageFrame::RenderLine(const LineInfo& line, Rect& frameRect)
	{
		const float textScale = FrameManager::Get().GetUIScale().y;
		const float lineHeight = GetFont()->GetHeight(textScale);

		// Store the render position for this line
		Point renderPos = frameRect.GetPosition();
		const_cast<LineInfo&>(line).renderPosition = renderPos;

		// Use hyperlink-aware text rendering if we have hyperlinks OR color changes
		if (!line.parsedText.hyperlinks.empty() || line.parsedText.colorChanges.size() > 1)
		{
			// Make a mutable copy for bounds calculation
			ParsedText mutableParsedText = line.parsedText;
			try 
			{
				GetFont()->DrawTextWithHyperlinks(mutableParsedText, renderPos, m_geometryBuffer, textScale, 
												  Color(line.message->r, line.message->g, line.message->b, 1.0f).GetARGB());
				
				// Update the original line's hyperlinks with calculated bounds
				const_cast<LineInfo&>(line).parsedText = mutableParsedText;
			}
			catch (...)
			{
				// Fallback to regular text rendering if hyperlink rendering fails
				GetFont()->DrawText(line.parsedText.plainText, renderPos, m_geometryBuffer, textScale, 
								   Color(line.message->r, line.message->g, line.message->b, 1.0f).GetARGB());
			}
		}
		else
		{
			// No hyperlinks or color changes, use regular text rendering
			GetFont()->DrawText(line.parsedText.plainText, renderPos, m_geometryBuffer, textScale, 
							   Color(line.message->r, line.message->g, line.message->b, 1.0f).GetARGB());
		}

		frameRect.top += lineHeight;
		return 1; // Always render as single line since we're handling wrapping ourselves
	}

	void ScrollingMessageFrame::OnMessagesChanged()
	{
		m_lineCache.clear();

		const float textScale = FrameManager::Get().GetUIScale().y;

		if (const FontPtr font = GetFont())
		{
			const Rect  contentRect = GetAbsoluteFrameRect();
			const Point position = contentRect.GetPosition();

			m_visibleLineCount = contentRect.GetHeight() / font->GetHeight(textScale);

			for (const auto& message : m_messages)
			{
				// Parse the entire message for hyperlinks and colors
				ParsedText parsedMessage = ParseTextMarkup(message.message, Color(message.r, message.g, message.b, 1.0f).GetARGB());
				
				// For simplicity, create line breaks based on text width
				// Split the parsed plain text into lines that fit within the frame width
				std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> wrappedLinesWithPositions;
				std::string currentLine;
				float currentWidth = 0.0f;
				const float maxWidth = contentRect.GetWidth();
				std::size_t currentOriginalPos = 0;
				std::size_t lineStartPos = 0;

				for (std::size_t i = 0; i < parsedMessage.plainText.length(); ++i)
				{
					char c = parsedMessage.plainText[i];
					
					if (c == '\n')
					{
						// Forced line break
						wrappedLinesWithPositions.push_back({currentLine, {lineStartPos, currentOriginalPos}});
						currentLine.clear();
						currentWidth = 0.0f;
						currentOriginalPos++;
						lineStartPos = currentOriginalPos;
						continue;
					}

					if (const FontGlyph* glyph = font->GetGlyphData(c))
					{
						float charWidth = glyph->GetAdvance(textScale);
						
						if (currentWidth + charWidth > maxWidth && !currentLine.empty())
						{
							// Line would be too long, break here
							wrappedLinesWithPositions.push_back({currentLine, {lineStartPos, currentOriginalPos}});
							currentLine = c;
							currentWidth = charWidth;
							lineStartPos = currentOriginalPos;
						}
						else
						{
							currentLine += c;
							currentWidth += charWidth;
						}
					}
					else
					{
						currentLine += c;
					}
					currentOriginalPos++;
				}

				if (!currentLine.empty())
				{
					wrappedLinesWithPositions.push_back({currentLine, {lineStartPos, currentOriginalPos}});
				}

				// Create LineInfo for each wrapped line with proper hyperlink and color mapping
				uint32 currentColor = Color(message.r, message.g, message.b, 1.0f).GetARGB(); // Track current color state
				
				for (const auto& lineData : wrappedLinesWithPositions)
				{
					const std::string& lineText = lineData.first;
					std::size_t lineStartInOriginal = lineData.second.first;
					std::size_t lineEndInOriginal = lineData.second.second;
					
					LineInfo lineInfo;
					lineInfo.line = lineText;
					lineInfo.message = &message;
					lineInfo.parsedText.plainText = lineText;
					
					// Start this line with the current color state
					lineInfo.parsedText.colorChanges.push_back({0, currentColor});
					
					// Copy relevant color changes for this line
					for (const auto& colorChange : parsedMessage.colorChanges)
					{
						std::size_t colorPos = colorChange.first;
						// If color change is within this line's range
						if (colorPos >= lineStartInOriginal && colorPos < lineEndInOriginal)
						{
							// Adjust position relative to line start
							std::size_t relativePos = colorPos - lineStartInOriginal;
							lineInfo.parsedText.colorChanges.push_back({relativePos, colorChange.second});
						}
					}
					
					// Update current color based on color changes that occur at or before the END of this line
					// This ensures next line inherits the correct color state
					for (const auto& colorChange : parsedMessage.colorChanges)
					{
						if (colorChange.first < lineEndInOriginal)
						{
							currentColor = colorChange.second;
						}
					}
					
					// Copy hyperlinks that intersect with this line (including partial hyperlinks)
					for (const auto& hyperlink : parsedMessage.hyperlinks)
					{
						// Check if hyperlink intersects with this line's range
						if (hyperlink.plainTextStart < lineEndInOriginal && 
							hyperlink.plainTextEnd > lineStartInOriginal)
						{
							Hyperlink lineHyperlink = hyperlink;
							
							// Calculate the portion of the hyperlink that appears in this line
							std::size_t hyperlinkStartInLine = 0;
							std::size_t hyperlinkEndInLine = lineText.length();
							
							if (hyperlink.plainTextStart >= lineStartInOriginal)
							{
								// Hyperlink starts within this line
								hyperlinkStartInLine = hyperlink.plainTextStart - lineStartInOriginal;
							}
							
							if (hyperlink.plainTextEnd <= lineEndInOriginal)
							{
								// Hyperlink ends within this line
								hyperlinkEndInLine = hyperlink.plainTextEnd - lineStartInOriginal;
							}
							
							// Only add if there's actually some text in this line
							if (hyperlinkStartInLine < hyperlinkEndInLine)
							{
								// Adjust positions relative to line start
								lineHyperlink.plainTextStart = hyperlinkStartInLine;
								lineHyperlink.plainTextEnd = hyperlinkEndInLine;
								
								// For multi-line hyperlinks, adjust the display text to show only the portion in this line
								lineHyperlink.displayText = lineText.substr(hyperlinkStartInLine, hyperlinkEndInLine - hyperlinkStartInLine);
								
								lineInfo.parsedText.hyperlinks.push_back(lineHyperlink);
							}
						}
					}
					
					// If no color changes were added beyond the initial one, remove the initial one to avoid duplicates
					if (lineInfo.parsedText.colorChanges.size() == 1)
					{
						// Keep the initial color
					}
					
					m_lineCache.push_back(lineInfo);
				}
			}
		}
		else
		{
			m_linePosition = 0;
			m_visibleLineCount = 0;
		}

		Invalidate(false);
	}

	void ScrollingMessageFrame::OnMouseDown(MouseButton button, int32 buttons, const Point& position)
	{
		// Call parent implementation first
		Frame::OnMouseDown(button, buttons, position);

		// Convert position to relative coordinates within the frame
		Rect frameRect = GetAbsoluteFrameRect();
		Point relativePos = position - frameRect.GetPosition();

		// Check if any hyperlink was clicked
		const float textScale = FrameManager::Get().GetUIScale().y;
		const float lineHeight = GetFont() ? GetFont()->GetHeight(textScale) : 16.0f;
		
		for (int i = m_linePosition; i < static_cast<int>(m_lineCache.size()) && i < m_linePosition + m_visibleLineCount; ++i)
		{
			if (i < 0 || i >= static_cast<int>(m_lineCache.size()))
				continue;
				
			const auto& line = m_lineCache[i];
			
			// Calculate the line's Y position relative to the frame
			int lineIndex = i - m_linePosition;
			float lineY = lineIndex * lineHeight;
			
			// Check if click is within this line's Y range
			if (relativePos.y < lineY || relativePos.y > lineY + lineHeight)
				continue;
				
			for (const auto& hyperlink : line.parsedText.hyperlinks)
			{
				// For multi-line hyperlinks, we need to check the X bounds more carefully
				// Since bounds might not be set correctly, use text positions as fallback
				
				// Calculate expected X position based on text position
				float expectedStartX = 0.0f;
				if (hyperlink.plainTextStart > 0 && GetFont())
				{
					std::string textBefore = line.parsedText.plainText.substr(0, hyperlink.plainTextStart);
					expectedStartX = GetFont()->GetTextWidth(textBefore, textScale);
				}
				
				float expectedEndX = expectedStartX;
				if (GetFont())
				{
					std::string hyperlinkText = line.parsedText.plainText.substr(hyperlink.plainTextStart, 
						hyperlink.plainTextEnd - hyperlink.plainTextStart);
					expectedEndX = expectedStartX + GetFont()->GetTextWidth(hyperlinkText, textScale);
				}
				
				// Create bounds for this hyperlink portion
				Rect hyperlinkBounds;
				hyperlinkBounds.left = expectedStartX;
				hyperlinkBounds.right = expectedEndX;
				hyperlinkBounds.top = lineY;
				hyperlinkBounds.bottom = lineY + lineHeight;
				
				if (hyperlinkBounds.IsPointInRect(relativePos))
				{
					// Trigger hyperlink click event
					TriggerEvent("HYPERLINK_CLICKED", this, hyperlink.type, hyperlink.payload);
					return;
				}
			}
		}
	}
}
