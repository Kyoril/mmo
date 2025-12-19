// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "map_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "math/degree.h"
#include "math/radian.h"
#include "editor_imgui_helpers.h"

namespace mmo
{
	namespace
	{
		static String s_mapInstanceType[] = {
			"Global",
			"Dungeon",
			"Raid",
			"Battleground",
			"Arena"
		};
	}

	MapEditorWindow::MapEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.maps, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Maps";
	}

	void MapEditorWindow::DrawDetailsImpl(proto::MapEntry& currentEntry)
	{
		// Top toolbar with actions
		const bool hasWorldFile = !currentEntry.directory().empty();
		ImGui::BeginDisabled(!hasWorldFile);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		if (ImGui::Button("Open in World Editor", ImVec2(180, 0)))
		{
			// Construct the world file path: Worlds/{name}/{name}.hwld
			const String worldName = currentEntry.directory();
			const String worldFilePath = "Worlds/" + worldName + "/" + worldName + ".hwld";
			m_host.OpenAsset(worldFilePath);
		}
		ImGui::PopStyleColor();
		ImGui::EndDisabled();
		ImGui::SameLine();
		DrawHelpMarker(hasWorldFile ? "Open this map's world file in the World Editor" : "No world file path specified");

		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Map Information", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Basic Details");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputText("##MapName", currentEntry.mutable_name());
			ImGui::SameLine();
			ImGui::Text("Map Name");
			ImGui::SameLine();
			DrawHelpMarker("Display name of the map");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputText("##MapPath", currentEntry.mutable_directory());
			ImGui::SameLine();
			ImGui::Text("World File Path");
			ImGui::SameLine();
			DrawHelpMarker("Path to the .hwld world file");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Instance Type");

			int instanceType = currentEntry.instancetype();
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
			ImGui::SetNextItemWidth(200);
			if (ImGui::Combo("##InstanceType", &instanceType, [](void*, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_mapInstanceType))
					{
						return false;
					}

					*out_text = s_mapInstanceType[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_mapInstanceType), -1))
			{
				currentEntry.set_instancetype(static_cast<mmo::proto::MapEntry_MapInstanceType>(instanceType));
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DrawHelpMarker("Type of map instance (Global, Dungeon, Raid, etc.)");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		// Creature spawns
		if (ImGui::CollapsingHeader("Creature Spawns"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Spawn Management");

			ImGui::BeginChild("creatureSpawns", ImVec2(-1, 0));
			ImGui::Columns(2, nullptr, true);
			static bool creatureSpawnWidthSet = false;
			if (!creatureSpawnWidthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
				creatureSpawnWidthSet = true;
			}

			static int currentCreatureSpawn = -1;

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("+ Add Creature Spawn", ImVec2(-1, 0)))
			{
				auto* spawn = currentEntry.mutable_unitspawns()->Add();
				spawn->set_positionx(0.0f);
				spawn->set_positiony(0.0f);
				spawn->set_positionz(0.0f);
				if (m_project.units.count() > 0)
				{
					spawn->set_unitentry(m_project.units.getTemplates().entry(0).id());
				}
				spawn->set_respawn(true);
				spawn->set_respawndelay(30 * 1000);
				spawn->set_isactive(true);
			}
			ImGui::PopStyleColor();

			ImGui::BeginDisabled(currentCreatureSpawn == -1 || currentCreatureSpawn >= currentEntry.unitspawns_size());
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
			if (ImGui::Button("Remove Spawn", ImVec2(-1, 0)))
			{
				currentEntry.mutable_unitspawns()->erase(currentEntry.mutable_unitspawns()->begin() + currentCreatureSpawn);
				currentCreatureSpawn = -1;
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::TextDisabled("%d creature spawns", currentEntry.unitspawns_size());

			ImGui::BeginChild("creatureSpawnListScrollable", ImVec2(-1, 0));

			// Custom list rendering to highlight invalid spawns
			for (int idx = 0; idx < currentEntry.unitspawns_size(); ++idx)
			{
				const auto& spawn = currentEntry.unitspawns(idx);
				const auto* unitEntry = m_project.units.getById(spawn.unitentry());
				const bool isInvalid = (unitEntry == nullptr);
				const bool isSelected = (currentCreatureSpawn == idx);

				ImGui::PushID(idx);

				// Highlight invalid spawns in red
				if (isInvalid)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.8f, 0.2f, 0.2f, 0.4f));
					ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.6f));
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0f, 0.4f, 0.4f, 0.8f));
				}

				std::ostringstream stream;
				stream << (isInvalid ? "[INVALID] " : "") << "#" << std::setw(6) << std::setfill('0') << spawn.unitentry();
				if (unitEntry != nullptr)
				{
					stream << " - " << unitEntry->name();
				}

				if (ImGui::Selectable(stream.str().c_str(), isSelected))
				{
					currentCreatureSpawn = idx;
				}

				if (isInvalid)
				{
					ImGui::PopStyleColor(4);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
						ImGui::Text("WARNING: Unit entry %d does not exist!", spawn.unitentry());
						ImGui::PopStyleColor();
						ImGui::EndTooltip();
					}
				}

				ImGui::PopID();
			}

			ImGui::EndChild();

			ImGui::NextColumn();

			// show editable details of selected creature spawn
			ImGui::BeginChild("creatureSpawnDetails", ImVec2(-1, -1));
			if (currentCreatureSpawn != -1 && currentCreatureSpawn < currentEntry.unitspawns_size())
			{
				auto* spawn = currentEntry.mutable_unitspawns()->Mutable(currentCreatureSpawn);
				DrawSectionHeader("Spawn Properties");

				// Check if unit entry is valid
				const auto* unitEntry = m_project.units.getById(spawn->unitentry());
				if (unitEntry == nullptr)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
					ImGui::TextWrapped("WARNING: This spawn references a non-existent unit entry (#%d)!", spawn->unitentry());
					ImGui::PopStyleColor();
					ImGui::Spacing();
				}

				ImGui::SetNextItemWidth(300);
				int unitEntryIdx = -1;
				for (int i = 0; i < m_project.units.count(); i++)
				{
					if (m_project.units.getTemplates().entry(i).id() == spawn->unitentry())
					{
						unitEntryIdx = i;
						break;
					}
				}

				if (ImGui::Combo("##UnitEntry", &unitEntryIdx, [](void* data, int idx, const char** out_text)
					{
						const auto* units = static_cast<proto::Units*>(data);
						const auto& entry = units->entry().at(idx);
						*out_text = entry.name().c_str();
						return true;
					}, &m_project.units.getTemplates(), m_project.units.count(), -1))
				{
					spawn->set_unitentry(m_project.units.getTemplates().entry(unitEntryIdx).id());
				}
				ImGui::SameLine();
				ImGui::Text("Unit Entry");
				ImGui::SameLine();
				DrawHelpMarker("The creature that will spawn at this location");

				ImGui::Spacing();

				float position[3] = { spawn->positionx(), spawn->positiony(), spawn->positionz() };
				ImGui::SetNextItemWidth(300);
				if (ImGui::InputFloat3("##Position", position, "%.3f"))
				{
					spawn->set_positionx(position[0]);
					spawn->set_positiony(position[1]);
					spawn->set_positionz(position[2]);
				}
				ImGui::SameLine();
				ImGui::Text("Spawn Position");
				ImGui::SameLine();
				DrawHelpMarker("X, Y, Z coordinates in the world");

				float rotation = Radian(spawn->rotation()).GetValueDegrees();
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputFloat("##Rotation", &rotation))
				{
					spawn->set_rotation(Degree(rotation).GetValueRadians());
				}
				ImGui::SameLine();
				ImGui::Text("Rotation (Degrees)");
				ImGui::SameLine();
				DrawHelpMarker("Facing direction in degrees (0-360)");

				ImGui::Spacing();
				ImGui::Spacing();
				DrawSectionHeader("Spawn Behavior");

				bool isActive = spawn->isactive();
				if (ImGui::Checkbox("Active##IsActive", &isActive))
				{
					spawn->set_isactive(isActive);
				}
				ImGui::SameLine();
				DrawHelpMarker("Whether this spawn is currently active");

				bool respawn = spawn->respawn();
				if (ImGui::Checkbox("Respawn##CanRespawn", &respawn))
				{
					spawn->set_respawn(respawn);
				}
				ImGui::SameLine();
				DrawHelpMarker("Whether the creature respawns after death");

				ImGui::BeginDisabled(!respawn);

				int32 respawnDelay = spawn->respawndelay() / 1000; // Convert to seconds
				ImGui::SetNextItemWidth(150);
				if (ImGui::InputInt("##RespawnDelay", &respawnDelay))
				{
					if (respawnDelay < 0) respawnDelay = 0;
					spawn->set_respawndelay(respawnDelay * 1000);
				}
				ImGui::SameLine();
				ImGui::Text("Respawn Delay (seconds)");
				ImGui::SameLine();
				DrawHelpMarker("Time in seconds before respawning");

				ImGui::EndDisabled();

				ImGui::Spacing();

				static const char* s_movementTypeStrings[] = {
					"Stationary",
					"Patrol",
					"Route"
				};

				int movement = spawn->movement();
				ImGui::SetNextItemWidth(200);
				if (ImGui::Combo("##Movement", &movement, [](void* data, int idx, const char** out_text)
					{
						*out_text = s_movementTypeStrings[idx];
						return true;
					}, nullptr, proto::UnitSpawnEntry_MovementType_MovementType_ARRAYSIZE, -1))
				{
					spawn->set_movement(static_cast<proto::UnitSpawnEntry_MovementType>(movement));
				}
				ImGui::SameLine();
				ImGui::Text("Movement Type");
				ImGui::SameLine();
				DrawHelpMarker("How the creature moves in the world");
			}
			else
			{
				ImGui::TextDisabled("Select a spawn to edit its properties");
			}
			ImGui::EndChild();

			ImGui::EndChild();

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}
	}
}
