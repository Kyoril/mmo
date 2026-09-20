// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "environment_profile_combo.h"

namespace mmo
{
	bool DrawEnvironmentProfileCombo(const proto::EnvironmentProfileManager& profiles, const char* label, const uint32 currentId,
		ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel)
	{
		const auto* current = currentId != 0 ? profiles.getById(currentId) : nullptr;

		String preview = noneLabel;
		if (current)
		{
			preview = current->name();
		}
		else if (currentId != 0)
		{
			preview = "(missing #" + std::to_string(currentId) + ")";
		}

		bool changed = false;
		if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLargest))
		{
			filter.Draw("##environment_profile_filter", -1.0f);

			if (ImGui::Selectable(noneLabel, currentId == 0))
			{
				setter(0);
				changed = currentId != 0;
				filter.Clear();
				ImGui::CloseCurrentPopup();
			}

			for (const auto& profile : profiles.getTemplates().entry())
			{
				if (filter.IsActive() && !filter.PassFilter(profile.name().c_str()))
				{
					continue;
				}

				ImGui::PushID(static_cast<int>(profile.id()));
				if (ImGui::Selectable(profile.name().c_str(), profile.id() == currentId))
				{
					setter(profile.id());
					changed = profile.id() != currentId;
					filter.Clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		return changed;
	}
}
