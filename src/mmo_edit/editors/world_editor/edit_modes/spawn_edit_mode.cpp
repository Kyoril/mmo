// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spawn_edit_mode.h"

#include <imgui.h>

#include "base/typedefs.h"
#include "base/utilities.h"
#include "math/plane.h"
#include "math/ray.h"
#include "scene_graph/camera.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/movable_object.h"
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

		// Clear selected spawn and destroy any waypoint visualization.
		SetSelectedSpawn(nullptr);

		m_worldEditor.RemoveAllUnitSpawns();
		m_worldEditor.ClearSelection();
	}

	void SpawnEditMode::OnViewportDrop(float x, float y)
	{
		ASSERT(m_mapEntry);

		WorldEditMode::OnViewportDrop(x, y);

		// Calculate spawn position
		Vector3 position;
		bool hitFound = false;

		// Try raycast against scene geometry
		Scene* scene = m_worldEditor.GetCamera().GetScene();
		if (scene)
		{
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);
			auto query = scene->CreateRayQuery(ray);
			query->SetSortByDistance(true);
			query->Execute();

			const auto& results = query->GetLastResult();
			float closestDist = std::numeric_limits<float>::max();

			for (const auto& entry : results)
			{
				if (hitFound && entry.distance > closestDist)
				{
					break;
				}

				if (entry.movable)
				{
					const ICollidable* collidable = entry.movable->GetCollidable();
					if (collidable)
					{
						CollisionResult hit;
						if (collidable->IsCollidable() && collidable->TestRayCollision(ray, hit))
						{
							if (hit.distance < closestDist)
							{
								closestDist = hit.distance;
								position = hit.contactPoint;
								hitFound = true;
							}
						}
					}
				}
			}
		}

		if (!hitFound)
		{
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

		// We only accept unitEntry drops
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UnitSpawn", ImGuiDragDropFlags_None))
		{
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
			entry->set_movement(proto::UnitSpawnEntry_MovementType_STATIONARY);
			entry->set_isactive(true);

			m_worldEditor.AddUnitSpawn(*entry, false);
		}
		else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ObjectSpawn", ImGuiDragDropFlags_None))
		{
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

	void SpawnEditMode::SetSelectedSpawn(proto::UnitSpawnEntry* spawn)
	{
		if (m_selectedUnitSpawn == spawn)
			return;

		m_selectedUnitSpawn = spawn;
		m_draggingWaypointIndex = -1;
		m_hoveredWaypointIndex = -1;
		m_waypointEditActive = false;

		RebuildWaypointVisualization();
	}

	void SpawnEditMode::SetWaypointEditActive(bool active)
	{
		m_waypointEditActive = active;
	}

	void SpawnEditMode::RebuildWaypointVisualization()
	{
		Scene* scene = m_worldEditor.GetCamera().GetScene();
		if (!scene)
		{
			return;
		}

		// Destroy existing path node/object.
		if (m_waypointPathNode)
		{
			m_waypointPathNode->DetachAllObjects();

			if (m_waypointPathObj)
			{
				scene->DestroyManualRenderObject(*m_waypointPathObj);
				m_waypointPathObj = nullptr;
			}

			for (auto* markerObj : m_waypointSpheres)
			{
				scene->DestroyManualRenderObject(*markerObj);
			}

			m_waypointSpheres.clear();

			for (auto* markerNode : m_waypointMarkerNodes)
			{
				markerNode->DetachAllObjects();
				scene->GetRootSceneNode().RemoveChild(*markerNode);
				scene->DestroySceneNode(*markerNode);
			}

			m_waypointMarkerNodes.clear();

			scene->GetRootSceneNode().RemoveChild(*m_waypointPathNode);
			scene->DestroySceneNode(*m_waypointPathNode);
			m_waypointPathNode = nullptr;
		}

		if (!m_selectedUnitSpawn || m_selectedUnitSpawn->waypoints_size() < 1)
		{
			return;
		}

		// Use a stable unique name based on the spawn's address.
		const String idStr = std::to_string(reinterpret_cast<uintptr_t>(m_selectedUnitSpawn));

		// Create scene node at world origin for the path lines.
		m_waypointPathNode = scene->GetRootSceneNode().CreateChildSceneNode("WaypointPathNode_" + idStr);

		// Create path render object and draw loop lines.
		m_waypointPathObj = scene->CreateManualRenderObject("WaypointPath_" + idStr);
		m_waypointPathObj->SetCastShadows(false);
		m_waypointPathNode->AttachObject(*m_waypointPathObj);

		{
			const auto lineOp = m_waypointPathObj->AddLineListOperation(
				MaterialManager::Get().Load("Editor/Wireframe.hmat"));

			const int wpCount = m_selectedUnitSpawn->waypoints_size();
			for (int i = 0; i < wpCount; ++i)
			{
				const auto& wp0 = m_selectedUnitSpawn->waypoints(i);
				const auto& wp1 = m_selectedUnitSpawn->waypoints((i + 1) % wpCount);

				lineOp->AddLine(
					Vector3(wp0.positionx(), wp0.positiony(), wp0.positionz()),
					Vector3(wp1.positionx(), wp1.positiony(), wp1.positionz()));
			}

			// lineOp goes out of scope here; its destructor calls Finish().
		}

		// Create one cross-marker render object per waypoint.
		const int wpCount = m_selectedUnitSpawn->waypoints_size();
		for (int i = 0; i < wpCount; ++i)
		{
			const auto& wp = m_selectedUnitSpawn->waypoints(i);
			const Vector3 center(wp.positionx(), wp.positiony(), wp.positionz());

			SceneNode* markerNode = scene->GetRootSceneNode().CreateChildSceneNode(
				"WaypointMarkerNode_" + idStr + "_" + std::to_string(i));

			ManualRenderObject* markerObj = scene->CreateManualRenderObject(
				"WaypointMarker_" + idStr + "_" + std::to_string(i));
			markerObj->SetCastShadows(false);
			markerNode->AttachObject(*markerObj);

			{
				const auto markerOp = markerObj->AddLineListOperation(
					MaterialManager::Get().Load("Editor/Wireframe.hmat"));

				constexpr float s = 0.5f;

				markerOp->AddLine(center + Vector3(-s, 0.0f, 0.0f), center + Vector3(s, 0.0f, 0.0f));
				markerOp->AddLine(center + Vector3(0.0f, -s, 0.0f), center + Vector3(0.0f, s, 0.0f));
				markerOp->AddLine(center + Vector3(0.0f, 0.0f, -s), center + Vector3(0.0f, 0.0f, s));

				// markerOp destructor calls Finish().
			}

			m_waypointSpheres.push_back(markerObj);
			m_waypointMarkerNodes.push_back(markerNode);
		}
	}

	int SpawnEditMode::HitTestWaypoint(float worldX, float worldY, float worldZ) const
	{
		if (!m_selectedUnitSpawn)
		{
			return -1;
		}

		const float hitRadiusSq = 1.5f * 1.5f;

		int closestIndex = -1;
		float closestDistSq = std::numeric_limits<float>::max();

		for (int i = 0; i < m_selectedUnitSpawn->waypoints_size(); ++i)
		{
			const auto& wp = m_selectedUnitSpawn->waypoints(i);

			const float dx = wp.positionx() - worldX;
			const float dy = wp.positiony() - worldY;
			const float dz = wp.positionz() - worldZ;
			const float distSq = dx * dx + dy * dy + dz * dz;

			if (distSq < hitRadiusSq && distSq < closestDistSq)
			{
				closestDistSq = distSq;
				closestIndex = i;
			}
		}

		return closestIndex;
	}

	void SpawnEditMode::OnMouseDown(float x, float y)
	{
		if (!m_waypointEditActive || !m_selectedUnitSpawn)
		{
			return;
		}

		// Raycast to world position using scene geometry, falling back to ground plane.
		Vector3 hitPos;
		bool hitFound = false;

		Scene* scene = m_worldEditor.GetCamera().GetScene();
		if (scene)
		{
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);
			auto query = scene->CreateRayQuery(ray);
			query->SetSortByDistance(true);
			query->Execute();

			const auto& results = query->GetLastResult();
			float closestDist = std::numeric_limits<float>::max();

			for (const auto& entry : results)
			{
				if (hitFound && entry.distance > closestDist)
				{
					break;
				}

				if (entry.movable)
				{
					const ICollidable* collidable = entry.movable->GetCollidable();
					if (collidable)
					{
						CollisionResult hit;
						if (collidable->IsCollidable() && collidable->TestRayCollision(ray, hit))
						{
							if (hit.distance < closestDist)
							{
								closestDist = hit.distance;
								hitPos = hit.contactPoint;
								hitFound = true;
							}
						}
					}
				}
			}
		}

		if (!hitFound)
		{
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);
			const auto plane = Plane(Vector3::UnitY, Vector3::Zero);
			const auto hit = ray.Intersects(plane);

			if (hit.first)
			{
				hitPos = ray.GetPoint(hit.second);
			}
			else
			{
				hitPos = ray.GetPoint(10.0f);
			}
		}

		// Check if the user clicked near an existing waypoint (begin drag).
		const int wpIndex = HitTestWaypoint(hitPos.x, hitPos.y, hitPos.z);
		if (wpIndex >= 0)
		{
			m_draggingWaypointIndex = wpIndex;
		}
		else
		{
			// Add a new waypoint at the hit position.
			auto* wp = m_selectedUnitSpawn->mutable_waypoints()->Add();
			wp->set_positionx(hitPos.x);
			wp->set_positiony(hitPos.y);
			wp->set_positionz(hitPos.z);
			wp->set_waittime(0);

			RebuildWaypointVisualization();
		}
	}

	void SpawnEditMode::OnMouseMoved(float x, float y)
	{
		if (m_draggingWaypointIndex < 0 || !m_selectedUnitSpawn)
		{
			return;
		}

		if (m_draggingWaypointIndex >= m_selectedUnitSpawn->waypoints_size())
		{
			m_draggingWaypointIndex = -1;
			return;
		}

		// Raycast to updated world position.
		Vector3 hitPos;
		bool hitFound = false;

		Scene* scene = m_worldEditor.GetCamera().GetScene();
		if (scene)
		{
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);
			auto query = scene->CreateRayQuery(ray);
			query->SetSortByDistance(true);
			query->Execute();

			const auto& results = query->GetLastResult();
			float closestDist = std::numeric_limits<float>::max();

			for (const auto& entry : results)
			{
				if (hitFound && entry.distance > closestDist)
				{
					break;
				}

				if (entry.movable)
				{
					const ICollidable* collidable = entry.movable->GetCollidable();
					if (collidable)
					{
						CollisionResult hit;
						if (collidable->IsCollidable() && collidable->TestRayCollision(ray, hit))
						{
							if (hit.distance < closestDist)
							{
								closestDist = hit.distance;
								hitPos = hit.contactPoint;
								hitFound = true;
							}
						}
					}
				}
			}
		}

		if (!hitFound)
		{
			const Ray ray = m_worldEditor.GetCamera().GetCameraToViewportRay(x, y, 10000.0f);
			const auto plane = Plane(Vector3::UnitY, Vector3::Zero);
			const auto hit = ray.Intersects(plane);

			if (hit.first)
			{
				hitPos = ray.GetPoint(hit.second);
			}
			else
			{
				return;
			}
		}

		// Update the dragged waypoint position and refresh visualization.
		auto* wp = m_selectedUnitSpawn->mutable_waypoints(m_draggingWaypointIndex);
		wp->set_positionx(hitPos.x);
		wp->set_positiony(hitPos.y);
		wp->set_positionz(hitPos.z);

		RebuildWaypointVisualization();
	}

	void SpawnEditMode::OnMouseUp(float x, float y)
	{
		m_draggingWaypointIndex = -1;
	}
}
