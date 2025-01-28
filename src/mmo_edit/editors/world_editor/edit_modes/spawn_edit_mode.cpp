// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spawn_edit_mode.h"

#include <imgui.h>

#include "base/typedefs.h"
#include "base/utilities.h"
#include "math/plane.h"
#include "math/ray.h"
#include "scene_graph/camera.h"

namespace mmo
{
	SpawnEditMode::SpawnEditMode(IWorldEditor& worldEditor, proto::MapManager& maps, proto::UnitManager& units, proto::ObjectManager& objects)
		: WorldEditMode(worldEditor)
		, m_maps(maps)
		, m_units(units)
		, m_objects(objects)
	{
	}

	const char* SpawnEditMode::GetName() const
	{
		static const char* s_name = "Spawn Editor";
		return s_name;
	}

	void SpawnEditMode::DrawDetails()
	{
		static const char* s_noneMap = "<None>";

		if (ImGui::BeginCombo("Map", m_mapEntry ? m_mapEntry->name().c_str() : s_noneMap, ImGuiComboFlags_None))
		{
			for (::uint32 i = 0; i < static_cast<uint32>(m_maps.count()); ++i)
			{
				ImGui::PushID(i);
				if (ImGui::Selectable(m_maps.getTemplates().entry(i).name().c_str(), m_maps.getTemplates().mutable_entry(i) == m_mapEntry))
				{
					proto::MapEntry* entry = m_maps.getTemplates().mutable_entry(i);
					if (m_mapEntry != entry)
					{
						m_mapEntry = entry;
						m_worldEditor.RemoveAllUnitSpawns();

						if (m_mapEntry)
						{
							// For each unit spawn on the map
							for (auto& unitSpawn : *m_mapEntry->mutable_unitspawns())
							{
								m_worldEditor.AddUnitSpawn(unitSpawn, false);
							}

							// For each object spawn on the map
							for (auto& objectSpawn : *m_mapEntry->mutable_objectspawns())
							{
								m_worldEditor.AddObjectSpawn(objectSpawn);
							}
						}
					}

				}
				ImGui::PopID();
			}

			ImGui::EndCombo();
		}

		if (ImGui::CollapsingHeader("Units"))
		{
			// Render a list of all zones
			if (ImGui::BeginListBox("##units"))
			{
				for (const auto& unit : m_units.getTemplates().entry())
				{
					ImGui::PushID(unit.id());
					if (ImGui::Selectable(unit.name().c_str(), &unit == m_selectedUnit))
					{
						m_selectedUnit = &unit;
					}

					ImGuiDragDropFlags src_flags = 0;
					src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;     // Keep the source displayed as hovered
					src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers; // Because our dragging is local, we disable the feature of opening foreign treenodes/tabs while dragging
					//src_flags |= ImGuiDragDropFlags_SourceNoPreviewTooltip; // Hide the tooltip
					if (ImGui::BeginDragDropSource(src_flags))
					{
						if (!(src_flags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
						{
							ImGui::Selectable(unit.name().c_str(), &unit == m_selectedUnit);
						}

						uint32 unitId = unit.id();
						ImGui::SetDragDropPayload("UnitSpawn", &unitId, sizeof(unitId));
						ImGui::EndDragDropSource();
					}

					ImGui::PopID();
				}

				ImGui::EndListBox();
			}
		}
		if (ImGui::CollapsingHeader("Objects"))
		{
			// Render a list of all zones
			if (ImGui::BeginListBox("##objects"))
			{
				for (const auto& object : m_objects.getTemplates().entry())
				{
					ImGui::PushID(object.id());
					ImGui::Selectable(object.name().c_str());

					ImGuiDragDropFlags src_flags = 0;
					src_flags |= ImGuiDragDropFlags_SourceNoDisableHover;     // Keep the source displayed as hovered
					src_flags |= ImGuiDragDropFlags_SourceNoHoldToOpenOthers; // Because our dragging is local, we disable the feature of opening foreign treenodes/tabs while dragging
					//src_flags |= ImGuiDragDropFlags_SourceNoPreviewTooltip; // Hide the tooltip
					if (ImGui::BeginDragDropSource(src_flags))
					{
						if (!(src_flags & ImGuiDragDropFlags_SourceNoPreviewTooltip))
						{
							ImGui::Selectable(object.name().c_str());
						}

						uint32 objectId = object.id();
						ImGui::SetDragDropPayload("ObjectSpawn", &objectId, sizeof(objectId));
						ImGui::EndDragDropSource();
					}

					ImGui::PopID();
				}

				ImGui::EndListBox();
			}
		}
	}

	void SpawnEditMode::OnDeactivate()
	{
		WorldEditMode::OnDeactivate();

		m_worldEditor.RemoveAllUnitSpawns();
		m_worldEditor.ClearSelection();
	}

	void SpawnEditMode::OnViewportDrop(float x, float y)
	{
		ASSERT(m_mapEntry);

		WorldEditMode::OnViewportDrop(x, y);

		// We only accept unitEntry drops
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UnitSpawn", ImGuiDragDropFlags_None))
		{
			Vector3 position;

			const auto plane = Plane(Vector3::UnitY, Vector3::Zero);
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);

			const auto hit = ray.Intersects(plane);
			if (hit.first)
			{
				position = ray.GetPoint(hit.second);
			}
			else
			{
				position = ray.GetPoint(10.0f);
			}

			// Snap to grid?
			if (m_worldEditor.IsGridSnapEnabled())
			{
				const float gridSize = m_worldEditor.GetTranslateGridSnapSize();

				// Snap position to grid size
				position.x = std::round(position.x / gridSize) * gridSize;
				position.y = std::round(position.y / gridSize) * gridSize;
				position.z = std::round(position.z / gridSize) * gridSize;
			}

			const uint32 unitId = *static_cast<uint32*>(payload->Data);
			ASSERT(unitId != 0);

			// Create unit spawn entity
			proto::UnitSpawnEntry* entry = m_mapEntry->mutable_unitspawns()->Add();
			entry->set_unitentry(unitId);
			entry->set_positionx(position.x);
			entry->set_positiony(position.y);
			entry->set_positionz(position.z);

			std::uniform_real_distribution rotationDistribution(0.0f, 2 * Pi);
			entry->set_rotation(rotationDistribution(randomGenerator));

			entry->set_respawn(true);
			entry->set_respawndelay(30000);
			entry->set_maxcount(1);
			entry->set_movement(proto::UnitSpawnEntry_MovementType_PATROL);
			entry->set_isactive(true);

			m_worldEditor.AddUnitSpawn(*entry, false);
		}
		else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ObjectSpawn", ImGuiDragDropFlags_None))
		{
			Vector3 position;

			const auto plane = Plane(Vector3::UnitY, Vector3::Zero);
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);

			const auto hit = ray.Intersects(plane);
			if (hit.first)
			{
				position = ray.GetPoint(hit.second);
			}
			else
			{
				position = ray.GetPoint(10.0f);
			}

			// Snap to grid?
			if (m_worldEditor.IsGridSnapEnabled())
			{
				const float gridSize = m_worldEditor.GetTranslateGridSnapSize();

				// Snap position to grid size
				position.x = std::round(position.x / gridSize) * gridSize;
				position.y = std::round(position.y / gridSize) * gridSize;
				position.z = std::round(position.z / gridSize) * gridSize;
			}

			const uint32 objectId = *static_cast<uint32*>(payload->Data);
			ASSERT(objectId != 0);

			// Create unit spawn entity
			proto::ObjectSpawnEntry* entry = m_mapEntry->mutable_objectspawns()->Add();
			entry->set_objectentry(objectId);
			entry->mutable_location()->set_positionx(position.x);
			entry->mutable_location()->set_positiony(position.y);
			entry->mutable_location()->set_positionz(position.z);

			Quaternion rotation = Quaternion::Identity;
			entry->mutable_location()->set_rotationw(rotation.w);
			entry->mutable_location()->set_rotationx(rotation.x);
			entry->mutable_location()->set_rotationy(rotation.y);
			entry->mutable_location()->set_rotationz(rotation.z);

			entry->set_respawn(true);
			entry->set_respawndelay(30000);
			entry->set_maxcount(1);
			entry->set_state(0);
			entry->set_isactive(true);

			m_worldEditor.AddObjectSpawn(*entry);
		}
	}
}
