// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_spell_entry_table.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>

namespace mmo
{
	void DrawUnitSpellEntryTable(const char* id, proto::Project& project, google::protobuf::RepeatedPtrField<proto::UnitSpellEntry>& spells)
	{
		static const char* s_itemNone = "<None>";

		ImGui::PushID(id);

		// Add button
		if (DrawSuccessButton("Add", ImVec2(-1, 0)))
		{
			auto* newEntry = spells.Add();
			newEntry->set_spellid(0);
		}

		if (ImGui::BeginTable("spells", 7, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_DefaultSort);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Min Initial Cooldown", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Max Initial Cooldown", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Min Cooldown", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Max Cooldown", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Probability", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (int index = 0; index < spells.size(); ++index)
			{
				auto* currentItem = spells.Mutable(index);

				ImGui::PushID(index);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();

				const uint32 spell = currentItem->spellid();

				const auto* spellEntry = project.spells.getById(spell);
				if (ImGui::BeginCombo("##spell", spellEntry != nullptr ? spellEntry->name().c_str() : s_itemNone, ImGuiComboFlags_None))
				{
					for (int i = 0; i < project.spells.count(); i++)
					{
						ImGui::PushID(i);
						const bool item_selected = project.spells.getTemplates().entry(i).id() == spell;
						const char* item_text = project.spells.getTemplates().entry(i).name().c_str();
						if (ImGui::Selectable(item_text, item_selected))
						{
							currentItem->set_spellid(project.spells.getTemplates().entry(i).id());
						}
						if (item_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}

				ImGui::TableNextColumn();

				int32 priority = currentItem->priority();
				if (ImGui::InputInt("##priority", &priority))
				{
					currentItem->set_priority(priority);
				}

				ImGui::TableNextColumn();

				int32 mininitialcooldown = currentItem->mininitialcooldown();
				if (ImGui::InputInt("ms##mininitialcooldown", &mininitialcooldown))
				{
					currentItem->set_mininitialcooldown(mininitialcooldown);
				}

				ImGui::TableNextColumn();

				int32 maxinitialcooldown = currentItem->maxinitialcooldown();
				if (ImGui::InputInt("ms##maxinitialcooldown", &maxinitialcooldown))
				{
					currentItem->set_maxinitialcooldown(maxinitialcooldown);
				}

				ImGui::TableNextColumn();

				int32 mincooldown = currentItem->mincooldown();
				if (ImGui::InputInt("ms##mincooldown", &mincooldown))
				{
					currentItem->set_mincooldown(mincooldown);
				}

				ImGui::TableNextColumn();

				int32 maxcooldown = currentItem->maxcooldown();
				if (ImGui::InputInt("ms##maxcooldown", &maxcooldown))
				{
					currentItem->set_maxcooldown(maxcooldown);
				}

				ImGui::TableNextColumn();

				int32 probability = currentItem->probability();
				if (ImGui::InputInt("%##probability", &probability))
				{
					currentItem->set_probability(probability);
				}

				ImGui::SameLine();

				if (DrawDangerButton("Remove"))
				{
					spells.erase(spells.begin() + index);
					index--;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::PopID();
	}
}
