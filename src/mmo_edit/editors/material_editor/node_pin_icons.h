#pragma once

#include "base/typedefs.h"

#include "imgui.h"

namespace mmo
{
	enum class IconType : uint32
	{
		Flow,
		FlowDown,
		Circle,
		Square,
		Grid,
		RoundSquare,
		Diamond
	};

	// Draws an icon into specified draw list.
	void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, uint32 color, uint32 innerColor);

	// Icon widget
	void Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f), const ImVec4& innerColor = ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
}
