// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "loot_assignment_widget.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>

#include "proto_data/project.h"

namespace mmo
{
	void DrawLootTableAssignment(proto::Project& project, ::google::protobuf::RepeatedField<::google::protobuf::uint32>& lootEntries)
	{
		ImGui::TextUnformatted("Loot Tables");
		ImGui::SameLine();
		DrawHelpMarker(
			"Loot tables assigned to this entry. Multiple loot tables can be assigned and each one is "
			"rolled independently when loot is generated, so common loot can be shared as a reusable module.");

		if (ImGui::BeginTable("lootTables", 2,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Loot Table", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();

			for (int index = 0; index < lootEntries.size(); ++index)
			{
				ImGui::PushID(index);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				const uint32 lootEntryId = lootEntries.Get(index);
				const auto* lootEntry = project.unitLoot.getById(lootEntryId);
				if (lootEntry != nullptr)
				{
					ImGui::Text("%u - %s", lootEntryId, lootEntry->name().c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%u - (Unknown Loot Table)", lootEntryId);
				}

				ImGui::TableNextColumn();
				if (DrawDangerButton("Remove"))
				{
					lootEntries.erase(lootEntries.begin() + index);
					index--;
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		// Add a new loot table via a combo box. Already assigned tables are filtered out.
		static const char* s_addLabel = "<Add Loot Table>";
		if (ImGui::BeginCombo("##addLootTable", s_addLabel, ImGuiComboFlags_None))
		{
			for (int i = 0; i < project.unitLoot.count(); ++i)
			{
				const auto& templateEntry = project.unitLoot.getTemplates().entry(i);
				const uint32 id = templateEntry.id();

				// Skip loot tables that are already assigned.
				bool alreadyAssigned = false;
				for (int assigned = 0; assigned < lootEntries.size(); ++assigned)
				{
					if (lootEntries.Get(assigned) == id)
					{
						alreadyAssigned = true;
						break;
					}
				}

				if (alreadyAssigned)
				{
					continue;
				}

				ImGui::PushID(i);
				if (ImGui::Selectable(templateEntry.name().c_str(), false))
				{
					lootEntries.Add(id);
				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}
	}
}
