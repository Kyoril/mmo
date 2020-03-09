// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "default_renderer.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	void DefaultRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		const std::string activeState = m_frame->IsEnabled() ? "Enabled" : "Disabled";

		const auto* imagery = m_frame->GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = m_frame->GetStateImageryByName("Enabled");
		}

		if (imagery)
		{
			imagery->Render(*m_frame);
		}
	}
}
