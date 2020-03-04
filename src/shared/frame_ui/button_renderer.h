// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_renderer.h"


namespace mmo
{
	/// This class acts as a default renderer for any given frame. It knows
	/// two states:
	///  * Normal
	///  * Hovered
	///	 * Pushed
	///  * Disabled
	class ButtonRenderer
		: public FrameRenderer
	{
	public:
		ButtonRenderer(const std::string& name)
			: FrameRenderer(name)
		{
		}
		virtual ~ButtonRenderer() = default;

	public:
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) override;
	};
}
