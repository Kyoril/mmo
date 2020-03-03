// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "default_renderer.h"
#include "frame.h"


namespace mmo
{
	void DefaultRenderer::Render(optional<Color> colorOverride, optional<Rect> clipper)
	{
		const std::string activeState = m_frame->IsEnabled() ? "Enabled" : "Disabled";
		// TODO: Get state imagery and call it's render method
	}
}
