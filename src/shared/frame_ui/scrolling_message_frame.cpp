
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
	}

	void ScrollingMessageFrame::ScrollUp()
	{
		m_linePosition = std::max(m_linePosition - 1, 0);
		Invalidate();
	}

	void ScrollingMessageFrame::ScrollDown()
	{
		m_linePosition = std::min(m_linePosition + 1, GetMessageCount() - 1);
		Invalidate();
	}

	void ScrollingMessageFrame::ScrollToTop()
	{
		m_linePosition = 0;
		Invalidate();
	}

	void ScrollingMessageFrame::ScrollToBottom()
	{
		m_linePosition = GetMessageCount() - 1;
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
		return m_linePosition >= GetMessageCount() - 1;
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
			for (int i = m_linePosition; i < GetMessageCount(); ++i)
			{
				const ScrollingMessageFrame::Message& message = GetMessageAt(i);
				int linesRendered = RenderMessage(message, frameRect);

				lineCount += linesRendered;
				if (lineCount > m_visibleLineCount)
				{
					break;
				}
			}
		}
	}

	int ScrollingMessageFrame::RenderMessage(const Message& message, Rect& frameRect)
	{
		const float textScale = FrameManager::Get().GetUIScale().y;
		const float lineHeight = GetFont()->GetHeight(textScale);

		int linesRendered = GetFont()->DrawText(message.message, frameRect, &m_geometryBuffer, textScale, Color(message.r, message.g, message.b, 1.0f));
		frameRect.top += lineHeight * linesRendered;

		return linesRendered;
	}

	void ScrollingMessageFrame::OnMessagesChanged()
	{
		m_lineCount = 0;

		const float textScale = FrameManager::Get().GetUIScale().y;

		FontPtr font = GetFont();
		if (font)
		{
			const Rect contentRect = GetAbsoluteFrameRect();
			m_visibleLineCount = contentRect.GetHeight() / font->GetHeight(textScale);

			for (const auto& message : m_messages)
			{
				m_lineCount += font->GetLineCount(message.message, contentRect, textScale);
			}

			if (m_linePosition >= m_lineCount)
			{
				m_linePosition = std::max(0, m_lineCount - 1);
			}
		}
		else
		{
			m_linePosition = 0;
			m_visibleLineCount = 0;
		}

		Invalidate(false);
	}
}
