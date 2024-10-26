
#include "scrolling_message_frame.h"
#include "frame_mgr.h"

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

		int linesRendered = GetFont()->DrawText(line.line, frameRect, &m_geometryBuffer, textScale, Color(line.message->r, line.message->g, line.message->b, 1.0f));
		frameRect.top += lineHeight * linesRendered;

		return linesRendered;
	}

	void ScrollingMessageFrame::OnMessagesChanged()
	{
		m_lineCache.clear();

		const float textScale = FrameManager::Get().GetUIScale().y;

		if (const FontPtr font = GetFont())
		{
			const Rect contentRect = GetAbsoluteFrameRect();
			m_visibleLineCount = contentRect.GetHeight() / font->GetHeight(textScale);

			for (const auto& message : m_messages)
			{
				int lineCount = 1;

				const float height = font->GetHeight(textScale);
				const float baseline = font->GetBaseline(textScale);
				const Point position = contentRect.GetPosition();

				float baseY = position.y + baseline;
				Point glyphPos(position);

				String line;

				for (size_t c = 0; c < message.message.length(); ++c)
				{
					size_t iterations = 1;

					char g = message.message[c];
					if (g == '\t')
					{
						g = ' ';
						iterations = 4;
					}

					const FontGlyph* glyph = nullptr;
					if ((glyph = font->GetGlyphData(g)))
					{
						const FontImage* const image = glyph->GetImage();
						glyphPos.y = baseY - (image->GetOffsetY() - image->GetOffsetY() * textScale) + 4;
						glyphPos.x += glyph->GetAdvance(textScale) * iterations;

						if (glyphPos.x >= contentRect.right)
						{
							m_lineCache.push_back({ line, &message });

							line.clear();
							line.push_back(g);

							glyphPos.x = position.x;
							baseY += height;
						}
						else
						{
							line.push_back(g);
						}
					}
				}

				if (!line.empty())
				{
					m_lineCache.push_back({ line, &message });
					line.clear();
				}
			}

			// Log it
			DLOG("New line count: " << m_lineCache.size() << " / " << m_visibleLineCount);
		}
		else
		{
			m_linePosition = 0;
			m_visibleLineCount = 0;
		}

		Invalidate(false);
	}
}
