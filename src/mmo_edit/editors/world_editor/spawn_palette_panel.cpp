// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spawn_palette_panel.h"
#include "edit_modes/spawn_edit_mode.h"

#include <imgui.h>
#include <iomanip>
#include <sstream>

#include "base/typedefs.h"

namespace mmo
{
	void SpawnPalettePanel::Draw(const String& id, SpawnEditMode* mode)
	{
		ImGui::Begin(id.c_str());

		if (mode == nullptr)
		{
			ImGui::TextDisabled("Spawn Edit Mode inactive.");
			ImGui::End();
			return;
		}

		static String s_unitSearchFilter;
		static String s_objectSearchFilter;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

		// Map Status Section
		if (ImGui::CollapsingHeader("Map Information", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			const String worldName = mode->ExtractWorldNameFromPath();

			ImGui::Text("World:");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", worldName.c_str());

			ImGui::Spacing();

			if (mode->GetMapEntry())
			{
				ImGui::Text("Map:");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s", mode->GetMapEntry()->name().c_str());

				ImGui::Spacing();
				ImGui::TextDisabled("Spawns: %d units, %d objects",
					mode->GetMapEntry()->unitspawns_size(),
					mode->GetMapEntry()->objectspawns_size());
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
				ImGui::TextWrapped("No map entry found for this world file.");
				ImGui::PopStyleColor();

				ImGui::Spacing();

				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("Create a map entry to enable spawning");
				ImGui::PopStyleColor();
			}

			ImGui::Unindent();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Units Section
		if (ImGui::CollapsingHeader("Available Units", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			// Search filter
			ImGui::SetNextItemWidth(-1);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
			ImGui::InputTextWithHint("##UnitSearch", "Search units...", &s_unitSearchFilter);
			ImGui::PopStyleColor();

			ImGui::Spacing();

			// Count filtered units
			int visibleCount = 0;
			for (const auto& unit : mode->GetUnits().getTemplates().entry())
			{
				if (s_unitSearchFilter.empty() ||
					unit.name().find(s_unitSearchFilter) != String::npos ||
					std::to_string(unit.id()).find(s_unitSearchFilter) != String::npos)
				{
					visibleCount++;
				}
			}

			ImGui::TextDisabled("Showing %d / %d units", visibleCount, mode->GetUnits().count());

			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.5f, 0.8f, 0.5f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 0.9f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.7f, 1.0f, 0.9f));

			if (ImGui::BeginListBox("##units", ImVec2(-1, 250)))
			{
				for (const auto& unit : mode->GetUnits().getTemplates().entry())
				{
					// Apply filter
					if (!s_unitSearchFilter.empty())
					{
						if (unit.name().find(s_unitSearchFilter) == String::npos &&
							std::to_string(unit.id()).find(s_unitSearchFilter) == String::npos)
						{
							continue;
						}
					}

					ImGui::PushID(unit.id());

					std::ostringstream labelStream;
					labelStream << "#" << std::setw(6) << std::setfill('0') << unit.id() << " - " << unit.name();

					if (ImGui::Selectable(labelStream.str().c_str(), &unit == mode->GetSelectedUnit()))
					{
						mode->SetSelectedUnit(&unit);
					}

					ImGuiDragDropFlags src_flags = 0;
					src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;
					src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

					if (ImGui::BeginDragDropSource(src_flags))
					{
						if (!(src_flags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
						{
							ImGui::Text("%s", unit.name().c_str());
						}

						uint32 unitId = unit.id();
						ImGui::SetDragDropPayload("UnitSpawn", &unitId, sizeof(unitId));
						ImGui::EndDragDropSource();
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Drag to viewport to spawn");
						ImGui::TextDisabled("ID: %u", unit.id());
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}

				ImGui::EndListBox();
			}

			ImGui::PopStyleColor(4);

			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Drag units to the viewport to spawn them");
			ImGui::PopStyleColor();

			ImGui::Unindent();
		}

		ImGui::Spacing();

		// Objects Section
		if (ImGui::CollapsingHeader("Available Objects", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();

			// Search filter
			ImGui::SetNextItemWidth(-1);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
			ImGui::InputTextWithHint("##ObjectSearch", "Search objects...", &s_objectSearchFilter);
			ImGui::PopStyleColor();

			ImGui::Spacing();

			// Count filtered objects
			int visibleObjectCount = 0;
			for (const auto& object : mode->GetObjects().getTemplates().entry())
			{
				if (s_objectSearchFilter.empty() ||
					object.name().find(s_objectSearchFilter) != String::npos ||
					std::to_string(object.id()).find(s_objectSearchFilter) != String::npos)
				{
					visibleObjectCount++;
				}
			}

			ImGui::TextDisabled("Showing %d / %d objects", visibleObjectCount, mode->GetObjects().count());

			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.3f, 0.7f, 0.5f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.4f, 0.8f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.5f, 0.9f, 0.9f));

			if (ImGui::BeginListBox("##objects", ImVec2(-1, 250)))
			{
				for (const auto& object : mode->GetObjects().getTemplates().entry())
				{
					// Apply filter
					if (!s_objectSearchFilter.empty())
					{
						if (object.name().find(s_objectSearchFilter) == String::npos &&
							std::to_string(object.id()).find(s_objectSearchFilter) == String::npos)
						{
							continue;
						}
					}

					ImGui::PushID(object.id());

					std::ostringstream labelStream;
					labelStream << "#" << std::setw(6) << std::setfill('0') << object.id() << " - " << object.name();

					ImGui::Selectable(labelStream.str().c_str());

					ImGuiDragDropFlags src_flags = 0;
					src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;
					src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

					if (ImGui::BeginDragDropSource(src_flags))
					{
						if (!(src_flags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
						{
							ImGui::Text("%s", object.name().c_str());
						}

						uint32 objectId = object.id();
						ImGui::SetDragDropPayload("ObjectSpawn", &objectId, sizeof(objectId));
						ImGui::EndDragDropSource();
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Drag to viewport to spawn");
						ImGui::TextDisabled("ID: %u", object.id());
						ImGui::EndTooltip();
					}

					ImGui::PopID();
				}

				ImGui::EndListBox();
			}

			ImGui::PopStyleColor(4);

			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Drag objects to the viewport to spawn them");
			ImGui::PopStyleColor();

			ImGui::Unindent();
		}

		ImGui::PopStyleVar(2);
		ImGui::End();
	}
}
