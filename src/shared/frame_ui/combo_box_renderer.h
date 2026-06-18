// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "frame_renderer.h"

namespace mmo
{
	class ComboBox;

	/// Renderer for the ComboBox frame.
	/// Uses the following StateImagery names:
	///   Normal   – collapsed, idle
	///   Hovered  – collapsed, mouse over
	///   Pushed   – collapsed, mouse pressed
	///   Open     – dropdown is open
	///   Disabled – frame is disabled
	class ComboBoxRenderer : public FrameRenderer
	{
	public:
		explicit ComboBoxRenderer(const std::string& name);
		~ComboBoxRenderer() override = default;

		void Render(optional<Color> colorOverride = optional<Color>(),
		            optional<Rect> clipper = optional<Rect>()) override;

		void NotifyFrameAttached() override;
		void NotifyFrameDetached() override;

	private:
		ComboBox* m_comboBox{ nullptr };
	};
}
