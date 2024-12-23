// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "checkbox_renderer.h"

#include "button.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	CheckboxRenderer::CheckboxRenderer(const std::string & name)
		: FrameRenderer(name)
		, m_pushed(false)
	{
	}
	void CheckboxRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		std::string activeState = "Disabled";
		if (m_frame->IsEnabled())
		{
			if (m_pushed)
			{
				activeState = "Pushed";
			}
			else if (m_frame->IsHovered())
			{
				activeState = "Hovered";
			}
			else
			{
				activeState = "Normal";
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
				imagery = m_frame->GetStateImageryByName("Normal");
			}
			else
			{
				imagery = m_frame->GetStateImageryByName("NormalChecked");
			}
		}

		if (imagery)
		{
			imagery->Render(m_frame->GetAbsoluteFrameRect(), Color::White);
		}
	}

	void CheckboxRenderer::NotifyFrameAttached()
	{
		m_frameConnections.disconnect();
		m_pushed = false;

		ASSERT(m_frame);
		
		m_button = dynamic_cast<Button*>(m_frame);

		m_frameConnections += m_frame->MouseDown.connect([this](const MouseEventArgs& args) {
			if (args.IsButtonPressed(MouseButton::Left))
			{
				this->m_pushed = true;
				this->m_frame->Invalidate();
			}
		});

		m_frameConnections += m_frame->MouseUp.connect([this](const MouseEventArgs& args) {
			if (!args.IsButtonPressed(MouseButton::Left))
			{
				this->m_pushed = false;
				this->m_frame->Invalidate();
			}
		});
	}

	void CheckboxRenderer::NotifyFrameDetached()
	{
		m_frameConnections.disconnect();
		m_pushed = false;
		m_button = nullptr;
	}
}
