// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "map_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "math/degree.h"
#include "math/radian.h"

namespace mmo
{
	static String s_mapInstanceType[] = {
		"Global",
		"Dungeon",
		"Raid",
		"Battleground",
		"Arena"
	};

	MapEditorWindow::MapEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorWindowBase(name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_toolbarButtonText = "Maps";
	}

	bool MapEditorWindow::Draw()
	{
		if (ImGui::Begin(m_name.c_str(), &m_visible))
		{
			ImGui::Columns(2, nullptr, true);
			static bool widthSet = false;
			if (!widthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
				widthSet = true;
			}

			static int currentItem = -1;

			if (ImGui::Button("Add new map", ImVec2(-1, 0)))
			{
				auto* map = m_project.maps.add();
				map->set_name("New Map");
			}

			ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.maps.count());
			if (ImGui::Button("Remove", ImVec2(-1, 0)))
			{
				m_project.maps.remove(m_project.maps.getTemplates().entry().at(currentItem).id());
			}
			ImGui::EndDisabled();

			ImGui::BeginChild("mapListScrollable", ImVec2(-1, 0));
			ImGui::ListBox("##mapList", &currentItem, [](void* data, int idx, const char** out_text)
				{
					const proto::Maps* maps = static_cast<proto::Maps*>(data);
					const auto& entry = maps->entry().at(idx);
					*out_text = entry.name().c_str();
					return true;

				}, &m_project.maps.getTemplates(), m_project.maps.count(), 20);
			ImGui::EndChild();

			ImGui::NextColumn();

			proto::MapEntry* currentEntry = nullptr;
			if (currentItem != -1 && currentItem < m_project.maps.count())
			{
				currentEntry = &m_project.maps.getTemplates().mutable_entry()->at(currentItem);
			}

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry->name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry->set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

			ImGui::BeginChild("mapDetails", ImVec2(-1, -1));
			if (currentEntry)
			{
				ImGui::InputText("Name", currentEntry->mutable_name());
				ImGui::InputText("Path", currentEntry->mutable_directory());

				int instanceType = currentEntry->instancetype();
				if (ImGui::Combo("Instance Type", &instanceType, [](void*, int idx, const char** out_text)
					{
						if (idx < 0 || idx >= IM_ARRAYSIZE(s_mapInstanceType))
						{
							return false;
						}

						*out_text = s_mapInstanceType[idx].c_str();
						return true;
					}, nullptr, IM_ARRAYSIZE(s_mapInstanceType), -1))
				{
					currentEntry->set_instancetype(static_cast<mmo::proto::MapEntry_MapInstanceType>(instanceType));
				}

				// Creature spawns
				if (ImGui::CollapsingHeader("Creature Spawns"))
				{
					ImGui::BeginChild("creatureSpawns", ImVec2(-1, 0));
					ImGui::Columns(2, nullptr, true);
					static bool widthSet = false;
					if (!widthSet)
					{
						ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
						widthSet = true;
					}

					static int currentCreatureSpawn = -1;

					if (ImGui::Button("Add new creature spawn", ImVec2(-1, 0)))
					{
						auto* spawn = currentEntry->mutable_unitspawns()->Add();
						spawn->set_positionx(0.0f);
						spawn->set_positiony(0.0f);
						spawn->set_positionz(0.0f);
						spawn->set_unitentry(m_project.units.getTemplates().entry(0).id());
						spawn->set_respawn(true);
						spawn->set_respawndelay(30 * 1000);
						spawn->set_isactive(true);
					}

					ImGui::BeginDisabled(currentCreatureSpawn == -1 || currentCreatureSpawn >= currentEntry->unitspawns_size());
					if (ImGui::Button("Remove", ImVec2(-1, 0)))
					{
						currentEntry->mutable_unitspawns()->erase(currentEntry->mutable_unitspawns()->begin() + currentCreatureSpawn);
						currentCreatureSpawn = -1;
					}
					ImGui::EndDisabled();

					ImGui::BeginChild("creatureSpawnListScrollable", ImVec2(-1, 0));
					ImGui::ListBox("##creatureSpawnList", &currentCreatureSpawn, [](void* data, int idx, const char** out_text)
					{
						const auto* spawns = static_cast<google::protobuf::RepeatedPtrField<proto::UnitSpawnEntry>*>(data);
						const auto& entry = spawns->at(idx);

						std::ostringstream stream;
						stream << "#" << std::setw(6) << std::setfill('0') << entry.unitentry();
						*out_text = stream.str().c_str();

						return true;
					}, currentEntry->mutable_unitspawns(), currentEntry->unitspawns_size(), 20);
					ImGui::EndChild();

					ImGui::NextColumn();

					// show editable details of selected creature spawn
					ImGui::BeginChild("creatureSpawnDetails", ImVec2(-1, -1));
					if (currentCreatureSpawn != -1 && currentCreatureSpawn < currentEntry->unitspawns_size())
					{
						auto* spawn = currentEntry->mutable_unitspawns()->Mutable(currentCreatureSpawn);

						float position[3] = { spawn->positionx(), spawn->positiony(), spawn->positionz() };
						if (ImGui::InputFloat3("Spawn Position", position, "%.3f"))
						{
							spawn->set_positionx(position[0]);
							spawn->set_positiony(position[1]);
							spawn->set_positionz(position[2]);
						}

						float rotation = Radian(spawn->rotation()).GetValueDegrees();
						if (ImGui::InputFloat("Spawn Rotation in Degrees", &rotation))
						{
							spawn->set_rotation(Degree(rotation).GetValueRadians());
						}

						bool isActive = spawn->isactive();
						if (ImGui::Checkbox("Active", &isActive))
						{
							spawn->set_isactive(isActive);
						}

						bool respawn = spawn->respawn();
						if (ImGui::Checkbox("Respawn", &respawn))
						{
							spawn->set_respawn(respawn);
						}

						ImGui::BeginDisabled(!respawn);

						int32 respawnDelay = spawn->respawndelay();
						if (ImGui::InputInt("Respawn Delay", &respawnDelay))
						{
							spawn->set_respawndelay(respawnDelay);
						}

						ImGui::EndDisabled();

						static const char* s_movementTypeStrings[] = {
							"Stationary",
							"Patrol",
							"Route"
						};

						int movement = spawn->movement();
						if (ImGui::Combo("Movement", &movement, [](void* data, int idx, const char** out_text)
							{
								*out_text = s_movementTypeStrings[idx];
								return true;
							}, nullptr, proto::UnitSpawnEntry_MovementType_MovementType_ARRAYSIZE, -1))
						{
							spawn->set_movement(static_cast<proto::UnitSpawnEntry_MovementType>(movement));
						}

						int unitEntry = spawn->unitentry();
						if (ImGui::Combo("Unit Entry", &unitEntry, [](void* data, int idx, const char** out_text)
						{
								const auto* units = static_cast<proto::Units*>(data);
								const auto& entry = units->entry().at(idx);
								*out_text = entry.name().c_str();
								return true;
							}, &m_project.units.getTemplates(), m_project.units.count(), -1))
						{
							spawn->set_unitentry(m_project.units.getTemplates().entry(unitEntry).id());
						}
					}
					ImGui::EndChild();

					ImGui::EndChild();
				}
			}
			ImGui::EndChild();

			ImGui::Columns(1);
		}
		ImGui::End();

		return false;
	}
}
