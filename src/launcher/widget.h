// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "color.h"
#include "geometry.h"
#include "text_renderer.h"

#include "base/typedefs.h"

#include <functional>
#include <string>

namespace mmo
{
	enum class ButtonState
	{
		Normal,
		Hovered,
		Pressed,
		Disabled
	};

	/// A clickable region. Draws either `text` or `iconId`, never both.
	///
	/// The launcher has exactly three buttons and will not grow more, so these are
	/// plain structs drawn by free functions rather than a widget framework. All
	/// rectangles are in logical units.
	struct Button
	{
		Rect rect;
		std::string text;
		uint32 iconId = 0;

		bool enabled = true;
		bool hovered = false;
		bool pressed = false;

		/// 0..1, eased over time so the hover glow does not pop.
		float hoverFade = 0.0f;

		std::function<void()> onClick;

		ButtonState GetState() const
		{
			if (!enabled)
			{
				return ButtonState::Disabled;
			}

			// Only show the pressed state while the cursor is still over the button:
			// dragging off a pressed button should visibly disarm it.
			if (pressed && hovered)
			{
				return ButtonState::Pressed;
			}

			return hovered ? ButtonState::Hovered : ButtonState::Normal;
		}
	};

	struct ProgressBar
	{
		Rect rect;
		/// What is drawn, eased toward `target`.
		float value = 0.0f;
		/// What the model last reported.
		float target = 0.0f;
	};

	struct Label
	{
		Rect rect;
		std::string text;
		TextAlign align = TextAlign::Left;
		Color color;
	};
}
