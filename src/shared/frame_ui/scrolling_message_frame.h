#pragma once

#include "frame.h"

namespace mmo
{
	class ScrollingMessageFrame : public Frame
	{
	public:

		/// @brief Represents a single message in the scrolling message frame.
		struct Message
		{
			std::string message;
			float r = 1.0f;
			float g = 1.0f;
			float b = 1.0f;
		};

	public:
		explicit ScrollingMessageFrame(const std::string& type, const std::string& name);
		virtual ~ScrollingMessageFrame() = default;

	public:
		virtual void Copy(Frame& other) override;

		void AddMessage(std::string message, float r, float g, float b);

		void ScrollUp();

		void ScrollDown();

		void ScrollToTop();

		void ScrollToBottom();

		void Clear();

		int GetMessageCount() const;

		bool IsAtTop() const;
		
		bool IsAtBottom() const;

		const Message& GetMessageAt(size_t index) const;

	private:
		void PopulateGeometryBuffer() override;


		struct LineInfo
		{
			String line;
			const Message* message;
		};

		int RenderLine(const LineInfo& line, Rect& frameRect);

	private:
		void OnMessagesChanged();

	private:
		std::vector<Message> m_messages;
		std::vector<LineInfo> m_lineCache;
		int m_linePosition = 0;
		int m_visibleLineCount = 0;
	};
}