#pragma once

#include "frame.h"
#include "hyperlink.h"

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

		/// Handles mouse click events to check for hyperlink clicks
		virtual void OnMouseDown(MouseButton button, int32 buttons, const Point& position) override;

	private:
		void PopulateGeometryBuffer() override;


		struct LineInfo
		{
			std::string line;
			const Message* message;
			ParsedText parsedText;
			Point renderPosition;
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