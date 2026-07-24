// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "textfield_renderer.h"
#include "state_imagery.h"
#include "frame.h"
#include "frame_mgr.h"
#include "geometry_helper.h"
#include "textfield.h"


namespace mmo
{
	TextFieldRenderer::TextFieldRenderer(const std::string & name)
		: FrameRenderer(name)
		, m_blinkCaret(true)
		, m_caretBlinkTimeout(0.66f)
		, m_caretBlinkElapsed(0.0f)
		, m_showCaret(true)
	{
	}

	void TextFieldRenderer::Update(float elapsedSeconds)
	{
		// only do the update if we absolutely have to
		if (m_blinkCaret && m_frame->HasInputCaptured())
		{
			m_caretBlinkElapsed += elapsedSeconds;

			if (m_caretBlinkElapsed > m_caretBlinkTimeout)
			{
				m_caretBlinkElapsed = 0.0f;
				m_showCaret ^= true;

				// state changed, so need a redraw
				m_frame->Invalidate();
			}
		}
	}

	void TextFieldRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		// Detect the current state
		std::string activeState = "Disabled";
		if (m_frame->IsEnabled())
		{
			activeState = "Enabled";
		}

		// Find the state imagery
		const auto* imagery = m_frame->GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = m_frame->GetStateImageryByName("Enabled");
		}

		// If found, draw the state imagery
		if (imagery)
		{
			imagery->Render(m_frame->GetAbsoluteFrameRect(), colorOverride.value_or(Color::White));
		}

		// TODO: Draw the text field selection background

		const float textScale = FrameManager::Get().GetTextScale();

		// Get the text field
		const auto* textField = dynamic_cast<TextField*>(m_frame);
		if (!textField)
		{
			return;
		}

		const auto& textAreaOffsets = textField->GetTextAreaOffset();

		// Get the frame rectangle
		auto frameRect = m_frame->GetAbsoluteFrameRect();

		auto scale = FrameManager::Get().GetUIScaleSize();
		frameRect.left += textAreaOffsets.left * scale.height;
		frameRect.top += textAreaOffsets.top * scale.height;
		frameRect.right -= textAreaOffsets.right * scale.height;
		frameRect.bottom -= textAreaOffsets.bottom * scale.height;

		// Retrieve the font
		const FontPtr font = m_frame->GetFont();
		if (font)
		{
			// Get the appropriate text to render
			const std::string* textToRender;
			std::string maskedText;
			
			if (textField->IsTextMasked())
			{
				// Use masked text
				maskedText = textField->GetVisualText();
				textToRender = &maskedText;
			}
			else
			{
				// Use parsed plain text (hyperlinks show as display text only)
				textToRender = &textField->GetParsedPlainText();
			}

			// Draw the text with clipping to the frame rectangle
			argb_t textColor = textField->IsEnabled(false) ?
				textField->GetEnabledTextColor().GetARGB() :
				textField->GetDisabledTextColor().GetARGB();

			if (textField->IsMultiLine())
			{
				// Draw the word-wrapped lines top-aligned, scrolled vertically.
				// Lines that don't fully fit into the text area are culled since
				// the geometry buffer has no clipping support.
				const auto& lines = textField->GetLineLayout();
				const float lineHeight = font->GetHeight(textScale);
				const float verticalScroll = textField->GetVerticalScrollOffset();

				for (std::size_t i = 0; i < lines.size(); ++i)
				{
					const float y = frameRect.top + static_cast<float>(i) * lineHeight - verticalScroll;
					if (y < frameRect.top - 0.5f)
					{
						continue;
					}
					if (y + lineHeight > frameRect.bottom + 0.5f)
					{
						break;
					}

					const auto& line = lines[i];
					if (line.endByte <= line.startByte)
					{
						continue;
					}

					const std::string lineText = textToRender->substr(line.startByte, line.endByte - line.startByte);
					Point linePos(frameRect.left, y);
					font->DrawText(lineText, linePos, m_frame->GetGeometryBuffer(), textScale, textColor);
				}
			}
			else
			{
				// Create a clipping area that accounts for scroll offset
				Rect textArea = frameRect;
				textArea.left -= textField->GetScrollOffset(); // Move the text start position left by scroll amount

				// Apply vertical alignment formatting
				const float textHeight = font->GetHeight(textScale);
				if (textField->GetVertAlignment() == VerticalAlignment::Center)
				{
					textArea.top += frameRect.GetHeight() * 0.5f - textHeight * 0.5f;
				}
				else if (textField->GetVertAlignment() == VerticalAlignment::Bottom)
				{
					textArea.top += frameRect.GetHeight() - textHeight;
				}

				// Use the position-based DrawText since area-based doesn't handle horizontal scrolling well
				Point textPos = textArea.GetPosition();
				font->DrawText(*textToRender, textPos, m_frame->GetGeometryBuffer(), textScale, textColor);
			}
		}

		// If the frame has captured user input...
		if (m_showCaret && m_frame->HasInputCaptured())
		{
			// Render the cursor with scroll offset
			const StateImagery* caretImagery = m_frame->GetStateImageryByName("Caret");
			if (caretImagery)
			{
				if (textField->IsMultiLine())
				{
					if (font)
					{
						// One-line-high caret at the caret's visual line and column
						const float lineHeight = font->GetHeight(textScale);
						const auto location = textField->GetCaretLineColumn();

						const float caretX = frameRect.left + textField->GetCaretPixelX();
						const float caretY = frameRect.top + static_cast<float>(location.line) * lineHeight -
							textField->GetVerticalScrollOffset();

						// Only show the cursor if its line is fully within the visible area
						if (caretY >= frameRect.top - 0.5f && caretY + lineHeight <= frameRect.bottom + 0.5f &&
							caretX <= frameRect.right)
						{
							Rect caretRect{ 0.0f, 0.0f, 2.0f, lineHeight };
							caretRect.Offset(Point(caretX, caretY));
							caretImagery->Render(caretRect, colorOverride.value_or(Color::White));
						}
					}
				}
				else
				{
					// Calculate cursor position accounting for scroll offset
					float cursorX = textField->GetCursorOffset() - textField->GetScrollOffset();

					// Only show cursor if it's within the visible area
					if (cursorX >= 0 && cursorX <= frameRect.GetWidth())
					{
						Rect caretRect{ 0.0f, 0.0f, 2.0f, frameRect.GetHeight()};
						caretRect.Offset(m_frame->GetAbsoluteFrameRect().GetPosition() +
							Point(cursorX + textAreaOffsets.left * scale.height, textAreaOffsets.top * scale.height));
						caretImagery->Render(caretRect, colorOverride.value_or(Color::White));
					}
				}
			}
		}
	}
}
