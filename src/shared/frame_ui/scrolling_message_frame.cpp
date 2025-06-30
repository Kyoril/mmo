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

		// Use hyperlink-aware text rendering if we have hyperlinks
		if (!line.parsedText.hyperlinks.empty())
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
			// No hyperlinks, use regular text rendering
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
				std::vector<std::string> wrappedLines;
				std::string currentLine;
				float currentWidth = 0.0f;
				const float maxWidth = contentRect.GetWidth();

				for (std::size_t i = 0; i < parsedMessage.plainText.length(); ++i)
				{
					char c = parsedMessage.plainText[i];
					
					if (c == '\n')
					{
						// Forced line break
						wrappedLines.push_back(currentLine);
						currentLine.clear();
						currentWidth = 0.0f;
						continue;
					}

					if (const FontGlyph* glyph = font->GetGlyphData(c))
					{
						float charWidth = glyph->GetAdvance(textScale);
						
						if (currentWidth + charWidth > maxWidth && !currentLine.empty())
						{
							// Line would be too long, break here
							wrappedLines.push_back(currentLine);
							currentLine = c;
							currentWidth = charWidth;
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
				}

				if (!currentLine.empty())
				{
					wrappedLines.push_back(currentLine);
				}

				// Create LineInfo for each wrapped line
				// For now, hyperlinks will only work if they don't span multiple lines
				for (const auto& lineText : wrappedLines)
				{
					LineInfo lineInfo;
					lineInfo.line = lineText;
					lineInfo.message = &message;
					
					// Create ParsedText for this line by filtering hyperlinks that are contained within it
					lineInfo.parsedText.plainText = lineText;
					lineInfo.parsedText.colorChanges.push_back({0, Color(message.r, message.g, message.b, 1.0f).GetARGB()});
					
					// Find hyperlinks that are completely contained in this line
					std::size_t lineStartInOriginal = parsedMessage.plainText.find(lineText);
					if (lineStartInOriginal != std::string::npos)
					{
						for (const auto& hyperlink : parsedMessage.hyperlinks)
						{
							// Simple check: if the hyperlink display text is contained in this line
							std::size_t hyperlinkPos = lineText.find(hyperlink.displayText);
							if (hyperlinkPos != std::string::npos)
							{
								Hyperlink lineHyperlink = hyperlink;
								// Adjust the hyperlink position relative to the line start
								lineInfo.parsedText.hyperlinks.push_back(lineHyperlink);
							}
						}
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
				// Check if bounds are valid
				if (hyperlink.bounds.GetWidth() <= 0 || hyperlink.bounds.GetHeight() <= 0)
					continue;
					
				// Convert hyperlink bounds from render coordinates to frame-relative coordinates
				Rect adjustedBounds = hyperlink.bounds;
				adjustedBounds.top = lineY;
				adjustedBounds.bottom = lineY + lineHeight;
				
				if (adjustedBounds.IsPointInRect(relativePos))
				{
					// Trigger hyperlink click event
					TriggerEvent("HYPERLINK_CLICKED", this, hyperlink.type, hyperlink.payload);
					return;
				}
			}
		}
	}
}
