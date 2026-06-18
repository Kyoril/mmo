// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "editor_windows/editor_entry_window_base.h"
#include "proto_data/project.h"
#include "scene_graph/manual_render_object.h"

namespace mmo
{
	class SceneNode;

	/// @brief Edit mode for placing and editing creature spawns in the world editor.
	/// Supports PATROL movement type waypoint editing: click to add waypoints, drag to reposition.
	class SpawnEditMode final : public WorldEditMode
	{
	public:
		explicit SpawnEditMode(IWorldEditor& worldEditor, proto::MapManager& maps, proto::UnitManager& units, proto::ObjectManager& objects);
		~SpawnEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnActivate() override;

		void OnDeactivate() override;

		bool SupportsViewportDrop() const override { return m_mapEntry != nullptr; }

		void OnViewportDrop(float x, float y) override;

		/// @brief Called when the left mouse button is pressed in the viewport.
		/// @param x Viewport-relative X in [0,1].
		/// @param y Viewport-relative Y in [0,1].
		void OnMouseDown(float x, float y) override;

		/// @brief Called every frame while the mouse moves over the viewport.
		/// @param x Viewport-relative X in [0,1].
		/// @param y Viewport-relative Y in [0,1].
		void OnMouseMoved(float x, float y) override;

		/// @brief Called when the left mouse button is released in the viewport.
		/// @param x Viewport-relative X in [0,1].
		/// @param y Viewport-relative Y in [0,1].
		void OnMouseUp(float x, float y) override;

	public:
		proto::MapEntry* GetMapEntry() const { return m_mapEntry; }

		/// @brief Sets the currently selected unit spawn for waypoint editing.
		/// @param spawn Pointer to the UnitSpawnEntry to select, or nullptr to deselect.
		void SetSelectedSpawn(proto::UnitSpawnEntry* spawn);

		/// @brief Returns true when a PATROL-movement spawn is selected and waypoint editing is active.
		[[nodiscard]] bool IsWaypointEditActive() const { return m_waypointEditActive; }

		/// @brief Explicitly enters or exits waypoint edit submode.
		void SetWaypointEditActive(bool active);

		/// @brief Returns true when a waypoint drag operation is in progress.
		[[nodiscard]] bool IsDraggingWaypoint() const { return m_draggingWaypointIndex >= 0; }

		/// @brief Accessor for the underlying IWorldEditor reference.
		IWorldEditor& GetWorldEditor() { return m_worldEditor; }

		/// @brief Destroys and rebuilds all waypoint visualization render objects from the current spawn.
		void RebuildWaypointVisualization();

	public:
		proto::UnitManager& GetUnits() const { return m_units; }
		proto::ObjectManager& GetObjects() const { return m_objects; }
		const proto::UnitEntry* GetSelectedUnit() const { return m_selectedUnit; }
		void SetSelectedUnit(const proto::UnitEntry* unit) { m_selectedUnit = unit; }
		String ExtractWorldNameFromPath() const;

	private:
		void DetectMapEntry();
		void CreateMapEntry(const String& worldName);

		/// @brief Tests world-space position (worldX, worldY, worldZ) against every waypoint.
		/// @param worldX World X coordinate to test.
		/// @param worldY World Y coordinate to test.
		/// @param worldZ World Z coordinate to test.
		/// @return Zero-based index of the nearest waypoint within 1.5 m, or -1 if none.
		int HitTestWaypoint(float worldX, float worldY, float worldZ) const;

	private:
		proto::MapManager& m_maps;

		proto::UnitManager& m_units;

		proto::ObjectManager& m_objects;

		proto::MapEntry* m_mapEntry = nullptr;

		const proto::UnitEntry* m_selectedUnit = nullptr;

		/// @brief True when a PATROL-movement spawn is selected.
		bool m_waypointEditActive = false;

		/// @brief Index of the waypoint currently being dragged, or -1 if none.
		int m_draggingWaypointIndex = -1;

		/// @brief Index of the waypoint currently hovered, or -1 if none.
		int m_hoveredWaypointIndex = -1;

		/// @brief The unit spawn whose waypoints are being edited, or nullptr.
		proto::UnitSpawnEntry* m_selectedUnitSpawn = nullptr;

		/// @brief ManualRenderObject used for the looping patrol path lines.
		ManualRenderObject* m_waypointPathObj = nullptr;

		/// @brief Scene node the waypoint path render object is attached to.
		SceneNode* m_waypointPathNode = nullptr;

		/// @brief One ManualRenderObject cross-marker per waypoint.
		std::vector<ManualRenderObject*> m_waypointSpheres;

		/// @brief Scene nodes for each waypoint cross-marker (parallel to m_waypointSpheres).
		std::vector<SceneNode*> m_waypointMarkerNodes;
	};
}
