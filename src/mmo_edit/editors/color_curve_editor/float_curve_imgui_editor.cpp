// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "float_curve_imgui_editor.h"

#include <algorithm>
#include <cmath>

namespace mmo
{
	namespace
	{
		constexpr float KeyRadius = 5.0f;
	}

	FloatCurveImGuiEditor::FloatCurveImGuiEditor(const char* label, FloatCurve& curve)
		: m_label(label ? label : "")
		, m_curve(curve)
	{
	}

	float FloatCurveImGuiEditor::TimeToX(float time, const ImVec2& pos, const ImVec2& size) const
	{
		return pos.x + time * size.x;
	}

	float FloatCurveImGuiEditor::ValueToY(float value, const ImVec2& pos, const ImVec2& size, float minV, float maxV) const
	{
		const float range = (maxV - minV) > 1e-5f ? (maxV - minV) : 1.0f;
		const float n = (value - minV) / range;
		return pos.y + (1.0f - n) * size.y;
	}

	float FloatCurveImGuiEditor::XToTime(float x, const ImVec2& pos, const ImVec2& size) const
	{
		if (size.x <= 0.0f)
		{
			return 0.0f;
		}
		float t = (x - pos.x) / size.x;
		return std::clamp(t, 0.0f, 1.0f);
	}

	float FloatCurveImGuiEditor::YToValue(float y, const ImVec2& pos, const ImVec2& size, float minV, float maxV) const
	{
		const float range = (maxV - minV) > 1e-5f ? (maxV - minV) : 1.0f;
		float n = 1.0f - (y - pos.y) / (size.y > 0.0f ? size.y : 1.0f);
		n = std::clamp(n, 0.0f, 1.0f);
		return minV + n * range;
	}

	bool FloatCurveImGuiEditor::Draw(float width, float height)
	{
		bool modified = false;

		ImGui::PushID(m_label.c_str());

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 canvasSize(width > 0.0f ? width : avail.x, height);
		const ImVec2 canvasPos = ImGui::GetCursorScreenPos();

		// Determine value range (auto-fit if not explicitly set).
		float minV = m_minValue;
		float maxV = m_maxValue;
		if (maxV <= minV)
		{
			minV = 0.0f;
			maxV = 1.0f;
			for (const auto& key : m_curve.GetKeys())
			{
				maxV = std::max(maxV, key.value);
			}
			maxV *= 1.1f;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Background + border
		drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(25, 25, 30, 255));
		drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(80, 80, 90, 255));

		// Grid lines (quarters)
		for (int i = 1; i < 4; ++i)
		{
			const float gx = canvasPos.x + canvasSize.x * (i / 4.0f);
			const float gy = canvasPos.y + canvasSize.y * (i / 4.0f);
			drawList->AddLine(ImVec2(gx, canvasPos.y), ImVec2(gx, canvasPos.y + canvasSize.y), IM_COL32(50, 50, 58, 255));
			drawList->AddLine(ImVec2(canvasPos.x, gy), ImVec2(canvasPos.x + canvasSize.x, gy), IM_COL32(50, 50, 58, 255));
		}

		// Reserve the canvas area for interaction.
		ImGui::InvisibleButton("##floatcurve_canvas", canvasSize);
		const bool hovered = ImGui::IsItemHovered();
		const ImGuiIO& io = ImGui::GetIO();

		// Sample the curve into a polyline.
		const int sampleCount = std::max(2, static_cast<int>(canvasSize.x));
		ImVec2 prev;
		for (int i = 0; i <= sampleCount; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(sampleCount);
			const float v = m_curve.Evaluate(t);
			const ImVec2 p(TimeToX(t, canvasPos, canvasSize), ValueToY(v, canvasPos, canvasSize, minV, maxV));
			if (i > 0)
			{
				drawList->AddLine(prev, p, IM_COL32(120, 200, 255, 255), 2.0f);
			}
			prev = p;
		}

		// Draw and interact with keys.
		const auto& keys = m_curve.GetKeys();
		int hoveredKey = -1;
		for (size_t i = 0; i < keys.size(); ++i)
		{
			const ImVec2 kp(TimeToX(keys[i].time, canvasPos, canvasSize),
				ValueToY(keys[i].value, canvasPos, canvasSize, minV, maxV));
			const float dx = io.MousePos.x - kp.x;
			const float dy = io.MousePos.y - kp.y;
			const bool over = (dx * dx + dy * dy) <= (KeyRadius + 3.0f) * (KeyRadius + 3.0f);
			if (over && hovered)
			{
				hoveredKey = static_cast<int>(i);
			}

			const ImU32 col = (static_cast<int>(i) == m_draggedKey || over)
				? IM_COL32(255, 230, 120, 255) : IM_COL32(220, 220, 220, 255);
			drawList->AddCircleFilled(kp, KeyRadius, col);
			drawList->AddCircle(kp, KeyRadius, IM_COL32(20, 20, 20, 255));
		}

		// Start dragging
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredKey >= 0)
		{
			m_draggedKey = hoveredKey;
		}

		// Dragging
		if (m_draggedKey >= 0)
		{
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				if (m_draggedKey < static_cast<int>(m_curve.GetKeyCount()))
				{
					const float newTime = XToTime(io.MousePos.x, canvasPos, canvasSize);
					const float newValue = YToValue(io.MousePos.y, canvasPos, canvasSize, minV, maxV);
					m_curve.UpdateKey(static_cast<size_t>(m_draggedKey), newTime, newValue);
					modified = true;
					// The key may have been re-sorted; re-find it by closest time so dragging stays smooth.
					int closest = 0;
					float best = 1e9f;
					for (size_t i = 0; i < m_curve.GetKeyCount(); ++i)
					{
						const float d = std::abs(m_curve.GetKey(i).time - newTime);
						if (d < best) { best = d; closest = static_cast<int>(i); }
					}
					m_draggedKey = closest;
				}
			}
			else
			{
				m_draggedKey = -1;
			}
		}

		// Add key on double-click of empty canvas.
		if (hovered && hoveredKey < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			const float t = XToTime(io.MousePos.x, canvasPos, canvasSize);
			const float v = YToValue(io.MousePos.y, canvasPos, canvasSize, minV, maxV);
			m_curve.AddKey(t, v);
			modified = true;
		}

		// Delete key on right-click.
		if (hovered && hoveredKey >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			if (m_curve.GetKeyCount() > 2)
			{
				m_curve.RemoveKey(static_cast<size_t>(hoveredKey));
				modified = true;
			}
		}

		// Range label
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2f", maxV);
		drawList->AddText(ImVec2(canvasPos.x + 3.0f, canvasPos.y + 2.0f), IM_COL32(150, 150, 160, 255), buf);
		std::snprintf(buf, sizeof(buf), "%.2f", minV);
		drawList->AddText(ImVec2(canvasPos.x + 3.0f, canvasPos.y + canvasSize.y - 16.0f), IM_COL32(150, 150, 160, 255), buf);

		ImGui::PopID();
		return modified;
	}
}
