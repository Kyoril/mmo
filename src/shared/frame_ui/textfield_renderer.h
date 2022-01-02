// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
		TextFieldRenderer(const std::string& name);
		virtual ~TextFieldRenderer() = default;

	public:
		virtual void Update(float elapsedSeconds) override;
		virtual void Render(
			optional<Color> colorOverride = optional<Color>(),
			optional<Rect> clipper = optional<Rect>()) override;

	private:
		/// true if the caret imagery should blink.
		bool m_blinkCaret;
		/// Time-out in seconds used for blinking the caret.
		float m_caretBlinkTimeout;
		/// Current time elapsed since last caret blink state change.
		float m_caretBlinkElapsed;

		bool m_showCaret;
	};
}
