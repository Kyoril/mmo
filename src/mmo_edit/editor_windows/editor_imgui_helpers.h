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

	/// @brief Draws a primary (blue) button with consistent editor styling.
	/// @param label Button label.
	/// @param size Optional button size.
	/// @return True if pressed.
	inline bool DrawPrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor();
		return pressed;
	}

	/// @brief Draws a primary (blue) small button with consistent editor styling.
	/// @param label Button label.
	/// @return True if pressed.
	inline bool DrawPrimarySmallButton(const char* label)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		const bool pressed = ImGui::SmallButton(label);
		ImGui::PopStyleColor();
		return pressed;
	}

	/// @brief Draws a danger (red) button with consistent editor styling.
	/// @param label Button label.
	/// @param size Optional button size.
	/// @return True if pressed.
	inline bool DrawDangerButton(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor();
		return pressed;
	}

	/// @brief Draws a danger (red) small button with consistent editor styling.
	/// @param label Button label.
	/// @return True if pressed.
	inline bool DrawDangerSmallButton(const char* label)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
		const bool pressed = ImGui::SmallButton(label);
		ImGui::PopStyleColor();
		return pressed;
	}

	/// @brief Draws a success (green) button with consistent editor styling.
	/// @param label Button label.
	/// @param size Optional button size.
	/// @return True if pressed.
	inline bool DrawSuccessButton(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor();
		return pressed;
	}

	/// @brief Draws a neutral (gray) button with consistent editor styling.
	/// @param label Button label.
	/// @param size Optional button size.
	/// @return True if pressed.
	inline bool DrawNeutralButton(const char* label, const ImVec2& size = ImVec2(0, 0))
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
		const bool pressed = ImGui::Button(label, size);
		ImGui::PopStyleColor();
		return pressed;
	}
}
