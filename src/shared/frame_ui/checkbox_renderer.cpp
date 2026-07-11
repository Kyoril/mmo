// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "checkbox_renderer.h"

#include "button.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	CheckboxRenderer::CheckboxRenderer(const std::string & name)
		: FrameRenderer(name)
	{
	}
	void CheckboxRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		std::string activeState = "Disabled";
		if (m_frame->IsEnabled(false))
		{
			activeState = "Normal";

			// Derive the visual state from the button state (like ButtonRenderer does), so
			// that hover/pushed visuals can also be driven from Lua via SetButtonState —
			// e.g. list rows forwarding hover from their non-clickable text children.
			if (m_button)
			{
				switch (m_button->GetButtonState())
				{
				case ButtonState::Pushed:
					activeState = "Pushed";
					break;
				case ButtonState::Hovered:
					activeState = "Hovered";
					break;
				default:
					break;
				}
			}
			else if (m_frame->IsHovered())
			{
				activeState = "Hovered";
			}
		}

		// Append "Checked" suffix to state
		if (m_button && m_button->IsChecked())
		{
			activeState += "Checked";
		}

		const auto* imagery = m_frame->GetStateImageryByName(activeState);
		if (!imagery)
		{
			if (m_button && m_button->IsChecked())
			{
				imagery = m_frame->GetStateImageryByName("NormalChecked");
			}
			else
			{
				imagery = m_frame->GetStateImageryByName("Normal");
			}
		}

		if (imagery)
		{
			imagery->Render(m_frame->GetAbsoluteFrameRect(), Color::White);
		}
	}

	void CheckboxRenderer::NotifyFrameAttached()
	{
		ASSERT(m_frame);

		m_button = dynamic_cast<Button*>(m_frame);
	}

	void CheckboxRenderer::NotifyFrameDetached()
	{
		m_button = nullptr;
	}
}
