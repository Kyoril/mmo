// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spawn_list_panel.h"
#include "edit_modes/spawn_edit_mode.h"

#include <imgui.h>
#include "imgui/misc/cpp/imgui_stdlib.h"
#include <sstream>

#include "base/typedefs.h"

namespace mmo
{
	void SpawnListPanel::Draw(const String& id, SpawnEditMode* mode)
	{
		ImGui::Begin(id.c_str());

		if (mode == nullptr)
		{
			ImGui::TextDisabled("Enter Spawn Edit mode (F4) to browse placed spawns.");
			ImGui::End();
			return;
		}

		proto::MapEntry* mapEntry = mode->GetMapEntry();
		if (mapEntry == nullptr)
		{
			ImGui::TextDisabled("No map entry for this world — nothing has been placed yet.");
			ImGui::End();
			return;
		}

		IWorldEditor& worldEditor = mode->GetWorldEditor();
		const void* selectedEntry = worldEditor.GetSelectedSpawnEntry();

		static String s_searchFilter;
		static bool s_showUnits = true;
		static bool s_showObjects = true;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

		// Search filter
		ImGui::SetNextItemWidth(-1);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
		ImGui::InputTextWithHint("##SpawnListSearch", "Search spawns by name or id...", &s_searchFilter);
		ImGui::PopStyleColor();

		// Type toggles
		ImGui::Checkbox("Units", &s_showUnits);
		ImGui::SameLine();
		ImGui::Checkbox("Objects", &s_showObjects);

		ImGui::Spacing();

		// Helper that decides whether a name/id passes the active text filter.
		const auto passesFilter = [](const String& name, uint32 entryId) -> bool
		{
			if (s_searchFilter.empty())
			{
				return true;
			}

			return name.find(s_searchFilter) != String::npos ||
				std::to_string(entryId).find(s_searchFilter) != String::npos;
		};

		int visibleCount = 0;
		const int totalCount = mapEntry->unitspawns_size() + mapEntry->objectspawns_size();

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.6f, 0.4f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.7f, 0.5f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.8f, 0.6f, 0.9f));

		// Reserve space at the bottom for the status text and hint.
		const float statusBarHeight = (ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y) * 2.0f;

		if (ImGui::BeginListBox("##spawnlist", ImVec2(-1, -statusBarHeight)))
		{
			int widgetId = 0;

			if (s_showUnits)
			{
				for (auto& spawn : *mapEntry->mutable_unitspawns())
				{
					const auto* unit = mode->GetUnits().getById(spawn.unitentry());
					const String unitName = unit ? unit->name() : String("<unknown unit>");

					if (!passesFilter(unitName, spawn.unitentry()))
					{
						continue;
					}

					++visibleCount;
					ImGui::PushID(widgetId++);

					std::ostringstream labelStream;
					labelStream << "[Unit] " << unitName << " (#" << spawn.unitentry() << ")";

					const bool isSelected = (selectedEntry == &spawn);
					if (ImGui::Selectable(labelStream.str().c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
					{
						worldEditor.SelectUnitSpawn(spawn);

						// Double-click both selects and jumps the camera to the spawn.
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							worldEditor.FocusSelection();
						}
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Click to select, double-click or press F to focus");
						ImGui::TextDisabled("Pos: %.1f, %.1f, %.1f", spawn.positionx(), spawn.positiony(), spawn.positionz());
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}
			}

			if (s_showObjects)
			{
				for (auto& spawn : *mapEntry->mutable_objectspawns())
				{
					const auto* object = mode->GetObjects().getById(spawn.objectentry());
					const String objectName = object ? object->name() : String("<unknown object>");

					if (!passesFilter(objectName, spawn.objectentry()))
					{
						continue;
					}

					++visibleCount;
					ImGui::PushID(widgetId++);

					std::ostringstream labelStream;
					labelStream << "[Obj] " << objectName << " (#" << spawn.objectentry() << ")";

					const bool isSelected = (selectedEntry == &spawn);
					if (ImGui::Selectable(labelStream.str().c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
					{
						worldEditor.SelectObjectSpawn(spawn);

						// Double-click both selects and jumps the camera to the spawn.
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						{
							worldEditor.FocusSelection();
						}
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Click to select, double-click or press F to focus");
						ImGui::TextDisabled("Pos: %.1f, %.1f, %.1f", spawn.location().positionx(), spawn.location().positiony(), spawn.location().positionz());
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}
			}

			ImGui::EndListBox();
		}

		ImGui::PopStyleColor(4);

		ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Showing %d / %d spawns", visibleCount, totalCount);

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("Click to select, then press F (or double-click) to focus the camera.");
		ImGui::PopStyleColor();

		ImGui::PopStyleVar(2);
		ImGui::End();
	}
}
