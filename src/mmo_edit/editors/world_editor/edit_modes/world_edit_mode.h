// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/filesystem.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"

// Forward-declare ImGui types so we don't drag imgui.h into every translation unit.
struct ImDrawList;
struct ImVec2;

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	class Quaternion;
	class Vector3;
	class Entity;
	class Camera;
	class WorldModelInstance;

	namespace proto
	{
		class ObjectSpawnEntry;
		class UnitSpawnEntry;
		class AreaTriggerEntry;
	}

	class IWorldEditor
	{
	public:
		virtual ~IWorldEditor() = default;

	public:
		virtual const std::filesystem::path GetWorldPath() const = 0;

		virtual void ClearSelection() = 0;

		virtual void RemoveAllUnitSpawns() = 0;

		virtual void AddUnitSpawn(proto::UnitSpawnEntry& spawn, bool select) = 0;

		virtual void AddObjectSpawn(proto::ObjectSpawnEntry& spawn) = 0;

		virtual void RemoveUnitSpawn(const proto::UnitSpawnEntry& spawn) = 0;

		virtual void RemoveObjectSpawn(const proto::ObjectSpawnEntry& spawn) = 0;

		/// @brief Selects an already-placed unit spawn (e.g. picked from a list) so it becomes the
		///        active selection and can be focused with the "F" key.
		/// @param spawn The unit spawn entry to select.
		virtual void SelectUnitSpawn(proto::UnitSpawnEntry& spawn) = 0;

		/// @brief Selects an already-placed object spawn (e.g. picked from a list) so it becomes the
		///        active selection and can be focused with the "F" key.
		/// @param spawn The object spawn entry to select.
		virtual void SelectObjectSpawn(proto::ObjectSpawnEntry& spawn) = 0;

		/// @brief Returns the proto entry pointer (UnitSpawnEntry or ObjectSpawnEntry) of the currently
		///        selected spawn, or nullptr if no spawn is selected. Used purely to highlight list entries.
		virtual const void* GetSelectedSpawnEntry() const = 0;

		/// @brief Moves the camera to focus on the current selection (identical to pressing the "F" key).
		///        Does nothing if the selection is empty.
		virtual void FocusSelection() = 0;

		virtual void AddAreaTrigger(proto::AreaTriggerEntry& trigger, bool select) = 0;

		virtual void RemoveAllAreaTriggers() = 0;

		virtual Camera& GetCamera() const = 0;

		virtual bool IsGridSnapEnabled() const = 0;

		virtual float GetTranslateGridSnapSize() const = 0;

		/// @brief Creates a mesh entity in the scene.
		/// @param assetName The mesh asset name to use (.hmsh file).
		/// @param position World position for the entity.
		/// @param orientation World orientation for the entity.
		/// @param scale World scale for the entity.
		/// @param objectId Unique object ID (0 to auto-generate).
		/// @return Pointer to the created entity, or nullptr on failure.
		virtual Entity* CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 objectId) = 0;

		/// @brief Creates a world model entity in the scene.
		/// @param assetName The world model asset name to use (.hwmo file).
		/// @param position World position for the entity.
		/// @param orientation World orientation for the entity.
		/// @param scale World scale for the entity.
		/// @param objectId Unique object ID (0 to auto-generate).
		/// @return Pointer to the created world model instance, or nullptr on failure.
		virtual WorldModelInstance* CreateWorldModelEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 objectId) = 0;

		virtual ManualRenderObject* CreateManualRenderObject(const String& name) = 0;
		virtual SceneNode* CreateChildSceneNode() = 0;
		virtual void DestroyManualRenderObject(const ManualRenderObject& obj) = 0;
		virtual void DestroySceneNode(const SceneNode& node) = 0;

		virtual bool HasTerrain() const = 0;

		virtual terrain::Terrain* GetTerrain() const = 0;

		virtual bool IsTransforming() const = 0;
	};

	class WorldEditMode : public NonCopyable
	{
	public:
		explicit WorldEditMode(IWorldEditor& worldEditor);
		virtual ~WorldEditMode() = default;

	public:
		virtual const char* GetName() const = 0;

		virtual void DrawDetails() = 0;

		virtual void OnActivate() {}

		virtual void OnDeactivate() {}

		virtual void OnMouseDown(float x, float y) {}

		virtual void OnMouseMoved(float x, float y) {}

		virtual void OnMouseUp(float x, float y) {}

		virtual void OnMouseHold(float deltaSeconds) {}

		virtual void OnMouseWheel(float delta) {}

		virtual bool SupportsViewportDrop() const { return false; }

		virtual void OnViewportDrop(float x, float y) {}

		/// @brief Called every frame while this mode is active to draw 2-D overlays directly
		///        onto the 3-D viewport using ImGui's draw list.
		/// @param drawList   The ImGui draw list for the viewport window (use to call AddLine, AddQuad, etc.).
		/// @param viewportMin  Top-left of the 3-D viewport in absolute screen coordinates.
		/// @param viewportSize Width/height of the 3-D viewport in pixels.
		virtual void DrawViewportOverlay(ImDrawList* /*drawList*/, const ImVec2& /*viewportMin*/, const ImVec2& /*viewportSize*/) {}

	protected:
		IWorldEditor& m_worldEditor;
	};
}
