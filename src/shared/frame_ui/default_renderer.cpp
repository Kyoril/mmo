// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "default_renderer.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	void DefaultRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		const std::string activeState = m_frame->IsEnabled() ? "Enabled" : "Disabled";
		
		const auto style = m_frame->GetStyle();
		if (style)
		{
			const auto* imagery = style->GetStateImageryByName(activeState);
			if (imagery)
			{
				imagery->Render(*m_frame);
			}
		}
	}
}
