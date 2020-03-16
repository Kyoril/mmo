// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "textfield_renderer.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
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
			imagery->Render();
		}

		// TODO: Draw the text field selection background

		// Draw the text field's text state imagery
		const auto* text = m_frame->GetStateImageryByName(m_frame->IsEnabled() ? "EnabledText" : "DisabledText");
		if (text)
		{
			text->Render();
		}

		// If the frame has captured user input...
		if (m_frame->HasInputCaptured())
		{
			// TODO: Render the caret at the given cursor position
		}
	}
}
