// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "button_renderer.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	void ButtonRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		std::string activeState = m_frame->IsEnabled() ? "Normal" : "Disabled";
		if (m_frame->IsHovered())
		{
			// TODO: Handle Pushed state
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
}
