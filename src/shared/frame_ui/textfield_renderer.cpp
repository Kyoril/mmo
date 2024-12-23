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

		const auto& textAreaOffsets = textField->GetTextAreaOffsets();

		// Get the frame rectangle
		auto frameRect = m_frame->GetAbsoluteFrameRect();
		frameRect.left += textAreaOffsets.left;
		frameRect.top += textAreaOffsets.top;
		frameRect.right -= textAreaOffsets.right;
		frameRect.bottom -= textAreaOffsets.bottom;

		// Retrieve the font
		const FontPtr font = m_frame->GetFont();
		if (font)
		{
			// We got a font, now draw the text
			const std::string& text = m_frame->GetVisualText();

			// Calculate the width for formatting
			const float textWidth = font->GetTextWidth(text, textScale);
			const float textHeight = font->GetHeight(textScale);

			// Calculate final text position in component
			Point position = frameRect.GetPosition();

			// Apply horizontal alignment
			if (textField->GetHorzAlignmnet() == HorizontalAlignment::Center)
			{
				position.x += frameRect.GetWidth() * 0.5f - textWidth * 0.5f;
			}
			else if (textField->GetHorzAlignmnet() == HorizontalAlignment::Right)
			{
				position.x += frameRect.GetWidth() - textWidth;
			}

			// Apply vertical alignment formatting
			if (textField->GetVertAlignment() == VerticalAlignment::Center)
			{
				position.y += frameRect.GetHeight() * 0.5f - textHeight * 0.5f;
			}
			else if (textField->GetVertAlignment() == VerticalAlignment::Bottom)
			{
				position.y += frameRect.GetHeight() - textHeight;
			}

			// Draw the text
			font->DrawText(text, position, m_frame->GetGeometryBuffer(), textScale);
		}

		// If the frame has captured user input...
		if (m_showCaret && m_frame->HasInputCaptured())
		{
			// TODO: Render the cursor
			const StateImagery* caretImagery = m_frame->GetStateImageryByName("Caret");
			if (caretImagery)
			{
				Rect caretRect{ 0.0f, 0.0f, 2.0f, frameRect.GetHeight()};
				caretRect.Offset(m_frame->GetAbsoluteFrameRect().GetPosition() + Point(textField->GetCursorOffset(), textField->GetTextAreaOffsets().top));
				caretImagery->Render(caretRect, Color(1.0f, 1.0f, 1.0f, 0.75f));
			}
		}
	}
}
