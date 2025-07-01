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
			imagery->Render(m_frame->GetAbsoluteFrameRect(), Color::White);
		}

		// TODO: Draw the text field selection background

		const float textScale = FrameManager::Get().GetUIScale().y;

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

			// Draw the text with clipping to the frame rectangle
			argb_t textColor = textField->IsEnabled(false) ? 
				textField->GetEnabledTextColor().GetARGB() : 
				textField->GetDisabledTextColor().GetARGB();
				
			// Use the position-based DrawText since area-based doesn't handle horizontal scrolling well
			Point textPos = textArea.GetPosition();
			font->DrawText(*textToRender, textPos, m_frame->GetGeometryBuffer(), textScale, textColor);
		}

		// If the frame has captured user input...
		if (m_showCaret && m_frame->HasInputCaptured())
		{
			// Render the cursor with scroll offset
			const StateImagery* caretImagery = m_frame->GetStateImageryByName("Caret");
			if (caretImagery)
			{
				// Calculate cursor position accounting for scroll offset
				float cursorX = textField->GetCursorOffset() - textField->GetScrollOffset();
				
				// Only show cursor if it's within the visible area
				if (cursorX >= 0 && cursorX <= frameRect.GetWidth())
				{
					Rect caretRect{ 0.0f, 0.0f, 2.0f, frameRect.GetHeight()};
					caretRect.Offset(m_frame->GetAbsoluteFrameRect().GetPosition() + 
						Point(cursorX + textAreaOffsets.left * scale.height, textAreaOffsets.top * scale.height));
					caretImagery->Render(caretRect, Color(1.0f, 1.0f, 1.0f, 0.75f));
				}
			}
		}
	}
}
