// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>
#include "math/degree.h"
#include "math/quaternion.h"

namespace mmo
{
	/// @brief Draws a styled section header with blue text and separator.
	/// @param text The header text to display.
	inline void DrawSectionHeader(const char* text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
		ImGui::Text("%s", text);
		ImGui::PopStyleColor();
		ImGui::Separator();
		ImGui::Spacing();
	}

	/// @brief Draws a help marker (?) that shows a tooltip on hover.
	/// @param desc The tooltip description text.
	inline void DrawHelpMarker(const char* desc)
	{
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	/// @brief Draws an editable quaternion rotation using Roll/Pitch/Yaw in degrees.
	/// @param label Section label shown above the controls.
	/// @param quaternion Quaternion to edit.
	/// @return True if edited.
	inline bool DrawQuaternionEulerDegreesControl(const char* label, Quaternion& quaternion)
	{
		bool changed = false;

		if (label && *label)
		{
			ImGui::TextUnformatted(label);
		}

		Rotator rotator = quaternion.ToRotator();
		float roll = rotator.roll.GetValueDegrees();
		float pitch = rotator.pitch.GetValueDegrees();
		float yaw = rotator.yaw.GetValueDegrees();

		changed |= ImGui::InputFloat("Roll", &roll);
		changed |= ImGui::InputFloat("Pitch", &pitch);
		changed |= ImGui::InputFloat("Yaw", &yaw);

		if (changed)
		{
			Rotator updated;
			updated.roll = Degree(roll);
			updated.pitch = Degree(pitch);
			updated.yaw = Degree(yaw);
			quaternion = Quaternion::FromRotator(updated);
		}

		return changed;
	}
}
