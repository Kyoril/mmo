// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_renderer.h"


namespace mmo
{
	/// This class acts as a default renderer for any given frame. It knows
	/// two states:
	///  * Enabled
	///  * Disabled
	class TextFieldRenderer
		: public FrameRenderer
	{
	public:
		TextFieldRenderer(const std::string& name)
			: FrameRenderer(name)
		{
		}
		virtual ~TextFieldRenderer() = default;

	public:
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) override;
	};
}
