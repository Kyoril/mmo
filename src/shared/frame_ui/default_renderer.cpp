// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "default_renderer.h"
#include "state_imagery.h"
#include "frame.h"


namespace mmo
{
	void DefaultRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		const std::string activeState = m_frame->IsEnabled(false) ? "Enabled" : "Disabled";

		const auto* imagery = m_frame->GetStateImageryByName(activeState);
		if (!imagery)
		{
			imagery = m_frame->GetStateImageryByName("Enabled");
		}

		if (imagery)
		{
			imagery->Render(m_frame->GetAbsoluteFrameRect(), colorOverride.value_or(Color::White));
		}
	}
}
