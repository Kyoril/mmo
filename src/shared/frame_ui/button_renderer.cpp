// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "button_renderer.h"

#include "button.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	ButtonRenderer::ButtonRenderer(const std::string & name)
		: FrameRenderer(name)
	{
	}

	void ButtonRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		std::string activeState = "Disabled";
		if (m_frame->IsEnabled(false) && m_button)
		{
			switch(m_button->GetButtonState())
			{
				case ButtonState::Pushed:
					activeState = "Pushed";
					break;
				case ButtonState::Hovered:
					activeState = "Hovered";
					break;
				case ButtonState::Normal:
					activeState = "Normal";
					break;
			}
		}

		const auto* imagery = m_frame->GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = m_frame->GetStateImageryByName("Normal");
		}

		if (imagery)
		{
			imagery->Render(m_frame->GetAbsoluteFrameRect(), colorOverride.value_or(Color::White));
		}
	}

	void ButtonRenderer::NotifyFrameAttached()
	{
		FrameRenderer::NotifyFrameAttached();

		m_button = dynamic_cast<Button*>(m_frame);
		ASSERT(m_button);
	}

	void ButtonRenderer::NotifyFrameDetached()
	{
		m_button = nullptr;

		FrameRenderer::NotifyFrameDetached();
	}
}
