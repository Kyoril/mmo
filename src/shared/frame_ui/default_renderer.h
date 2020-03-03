// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_renderer.h"


namespace mmo
{
	/// This class acts as a default renderer for any given frame. It knows
	/// two states:
	///  * Enabled
	///  * Disabled
	class DefaultRenderer
		: public FrameRenderer
	{
	public:
		DefaultRenderer(const std::string& name)
			: FrameRenderer(name)
		{
		}
		virtual ~DefaultRenderer() = default;

	public:
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) override;
	};
}
