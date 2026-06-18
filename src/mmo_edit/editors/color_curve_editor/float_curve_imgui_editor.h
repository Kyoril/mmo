// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/float_curve.h"

#include <imgui.h>
#include <string>

namespace mmo
{
	/**
	 * @class FloatCurveImGuiEditor
	 * @brief Compact ImGui editor widget for a scalar @ref FloatCurve (e.g. size/alpha over life).
	 *
	 * Interaction:
	 *   - Left-drag a key to move it in time/value.
	 *   - Double-click on empty canvas to add a key.
	 *   - Right-click a key to delete it (a minimum of two keys is kept).
	 */
	class FloatCurveImGuiEditor
	{
	public:
		FloatCurveImGuiEditor(const char* label, FloatCurve& curve);

		/// @brief Draws the editor. Returns true if the curve was modified this frame.
		bool Draw(float width = 0.0f, float height = 140.0f);

		/// @brief Sets the visible value range on the Y axis. If max <= min the range auto-fits.
		void SetValueRange(float minValue, float maxValue) { m_minValue = minValue; m_maxValue = maxValue; }

	private:
		float TimeToX(float time, const ImVec2& pos, const ImVec2& size) const;
		float ValueToY(float value, const ImVec2& pos, const ImVec2& size, float minV, float maxV) const;
		float XToTime(float x, const ImVec2& pos, const ImVec2& size) const;
		float YToValue(float y, const ImVec2& pos, const ImVec2& size, float minV, float maxV) const;

	private:
		std::string m_label;
		FloatCurve& m_curve;
		int m_draggedKey { -1 };
		float m_minValue { 0.0f };
		float m_maxValue { 0.0f };  ///< <= m_minValue means auto-fit
	};
}
