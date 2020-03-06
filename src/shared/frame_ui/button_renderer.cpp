// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "button_renderer.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	ButtonRenderer::ButtonRenderer(const std::string & name)
		: FrameRenderer(name)
		, m_pushed(false)
	{
	}
	void ButtonRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		std::string activeState = m_frame->IsEnabled() ? "Normal" : "Disabled";
		if (m_pushed)
		{
			activeState = "Pushed";
		}
		else if (m_frame->IsHovered())
		{
			activeState = "Hovered";
		}

		const auto style = m_frame->GetStyle();
		if (style)
		{
			const auto* imagery = style->GetStateImageryByName(activeState);
			if (!imagery)
			{
				imagery = style->GetStateImageryByName("Normal");
			}

			if (imagery)
			{
				imagery->Render(*m_frame);
			}
		}
	}

	void ButtonRenderer::NotifyFrameAttached()
	{
		m_frameConnections.disconnect();
		m_pushed = false;

		ASSERT(m_frame);

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

	void ButtonRenderer::NotifyFrameDetached()
	{
		m_frameConnections.disconnect();
		m_pushed = false;
	}
}
