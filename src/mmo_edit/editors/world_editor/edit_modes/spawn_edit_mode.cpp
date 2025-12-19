// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spawn_edit_mode.h"

#include <imgui.h>

#include "base/typedefs.h"
#include "base/utilities.h"
#include "math/plane.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "terrain/terrain.h"

namespace mmo
{
	SpawnEditMode::SpawnEditMode(IWorldEditor& worldEditor, proto::MapManager& maps, proto::UnitManager& units, proto::ObjectManager& objects)
		: WorldEditMode(worldEditor)
		, m_maps(maps)
		, m_units(units)
		, m_objects(objects)
	{
		DetectMapEntry();
	}

	const char* SpawnEditMode::GetName() const
	{
		static const char* s_name = "Spawn Editor";
		return s_name;
	}

	void SpawnEditMode::DrawDetails()
	{
		static String s_unitSearchFilter;
		static String s_objectSearchFilter;

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

		// Map Status Section
		if (ImGui::CollapsingHeader("Map Information", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			
			const String worldName = ExtractWorldNameFromPath();
			
			ImGui::Text("World:");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", worldName.c_str());
			
			ImGui::Spacing();
			
			if (m_mapEntry)
			{
				ImGui::Text("Map:");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "%s", m_mapEntry->name().c_str());
				
				ImGui::Spacing();
				ImGui::TextDisabled("Spawns: %d units, %d objects", 
					m_mapEntry->unitspawns_size(), 
					m_mapEntry->objectspawns_size());
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
				ImGui::TextWrapped("No map entry found for this world file.");
				ImGui::PopStyleColor();
				
				ImGui::Spacing();
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.4f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
				if (ImGui::Button("+ Create Map Entry", ImVec2(200, 0)))
				{
					CreateMapEntry(worldName);
				}
				ImGui::PopStyleColor(3);
				
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
			for (const auto& unit : m_units.getTemplates().entry())
			{
				if (s_unitSearchFilter.empty() || 
					unit.name().find(s_unitSearchFilter) != String::npos ||
					std::to_string(unit.id()).find(s_unitSearchFilter) != String::npos)
				{
					visibleCount++;
				}
			}
			
			ImGui::TextDisabled("Showing %d / %d units", visibleCount, m_units.count());
			
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.5f, 0.8f, 0.5f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.6f, 0.9f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.4f, 0.7f, 1.0f, 0.9f));
			
			if (ImGui::BeginListBox("##units", ImVec2(-1, 250)))
			{
				for (const auto& unit : m_units.getTemplates().entry())
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
					
					if (ImGui::Selectable(labelStream.str().c_str(), &unit == m_selectedUnit))
					{
						m_selectedUnit = &unit;
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
			ImGui::InputTextWithHint("##ObjectSearch", "🔍 Search objects...", &s_objectSearchFilter);
			ImGui::PopStyleColor();
			
			ImGui::Spacing();
			
			// Count filtered objects
			int visibleObjectCount = 0;
			for (const auto& object : m_objects.getTemplates().entry())
			{
				if (s_objectSearchFilter.empty() || 
					object.name().find(s_objectSearchFilter) != String::npos ||
					std::to_string(object.id()).find(s_objectSearchFilter) != String::npos)
				{
					visibleObjectCount++;
				}
			}
			
			ImGui::TextDisabled("Showing %d / %d objects", visibleObjectCount, m_objects.count());
			
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.5f, 0.3f, 0.7f, 0.5f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.4f, 0.8f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.7f, 0.5f, 0.9f, 0.9f));
			
			if (ImGui::BeginListBox("##objects", ImVec2(-1, 250)))
			{
				for (const auto& object : m_objects.getTemplates().entry())
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
	}

	void SpawnEditMode::OnActivate()
	{
		WorldEditMode::OnActivate();

		// Load spawns when entering spawn mode
		if (m_mapEntry)
		{
			for (auto& unitSpawn : *m_mapEntry->mutable_unitspawns())
			{
				m_worldEditor.AddUnitSpawn(unitSpawn, false);
			}

			for (auto& objectSpawn : *m_mapEntry->mutable_objectspawns())
			{
				m_worldEditor.AddObjectSpawn(objectSpawn);
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

			const auto hitResult = m_worldEditor.GetTerrain()->RayIntersects(ray);
			if (hitResult.first)
			{
				position = hitResult.second.position;
			}
			else
			{
				const auto hit = ray.Intersects(plane);
				if (hit.first)
				{
					position = ray.GetPoint(hit.second);
				}
				else
				{
					position = ray.GetPoint(10.0f);
				}
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

			const auto hitResult = m_worldEditor.GetTerrain()->RayIntersects(ray);
			if (hitResult.first)
			{
				position = hitResult.second.position;
			}
			else
			{
				const auto hit = ray.Intersects(plane);
				if (hit.first)
				{
					position = ray.GetPoint(hit.second);
				}
				else
				{
					position = ray.GetPoint(10.0f);
				}
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

	void SpawnEditMode::DetectMapEntry()
	{
		const String worldName = ExtractWorldNameFromPath();
		
		if (worldName.empty())
		{
			return;
		}

		// Search for a map entry with matching directory
		for (::uint32 i = 0; i < static_cast<uint32>(m_maps.count()); ++i)
		{
			auto* entry = m_maps.getTemplates().mutable_entry(i);
			if (entry->directory() == worldName)
			{
				m_mapEntry = entry;
				return;
			}
		}
	}

	void SpawnEditMode::CreateMapEntry(const String& worldName)
	{
		// Generate a new unique ID for the map
		uint32 newId = 1;
		for (::uint32 i = 0; i < static_cast<uint32>(m_maps.count()); ++i)
		{
			const auto& entry = m_maps.getTemplates().entry(i);
			if (entry.id() >= newId)
			{
				newId = entry.id() + 1;
			}
		}
		
		// Create new map entry
		auto* newEntry = m_maps.add(newId);
		newEntry->set_name(worldName);
		newEntry->set_directory(worldName);
		newEntry->set_instancetype(proto::MapEntry_MapInstanceType_GLOBAL);

		m_mapEntry = newEntry;
	}

	String SpawnEditMode::ExtractWorldNameFromPath() const
	{
		const auto worldPath = m_worldEditor.GetWorldPath();
		
		// Expected format: Worlds/{name}/{name}.hwld
		// Extract the directory name (second to last component)
		if (worldPath.has_parent_path())
		{
			const auto parentPath = worldPath.parent_path();
			if (parentPath.has_filename())
			{
				return parentPath.filename().string();
			}
		}
		
		return "";
	}
}
