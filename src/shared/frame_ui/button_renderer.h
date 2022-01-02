// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_renderer.h"

#include "base/signal.h"


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
		ButtonRenderer(const std::string& name);
		virtual ~ButtonRenderer() = default;

	public:
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) override;
		virtual void NotifyFrameAttached() override;
		virtual void NotifyFrameDetached() override;

	private:
		/// Whether the frame is currently pushed.
		bool m_pushed;
		/// Frame signal connections.
		scoped_connection_container m_frameConnections;
	};
}
