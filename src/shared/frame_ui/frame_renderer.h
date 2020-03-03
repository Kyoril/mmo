// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/optional.h"


namespace mmo
{
	class Frame;
	class Color;
	class Rect;


	/// Interface for a frame renderer. Frame renderers populate geometry buffers
	/// to render frames. Each frame might have several unique states that the
	/// frame renderer knows and applies.
	class IFrameRenderer
		: public NonCopyable
	{
	public:
		/// Virtual default destructor.
		virtual ~IFrameRenderer() = default;

	public:
		/// Renders a given frame using this renderer instance.
		/// @param frame The frame instance to render.
		/// @param colorOverride An optional color override for tinting.
		/// @param clipper An optional clip rect.
		virtual void Render(
			Frame& frame, 
			optional<Color> colorOverride = optional<Color>(), 
			optional<Rect> clipper = optional<Rect>()) = 0;
	};
}
