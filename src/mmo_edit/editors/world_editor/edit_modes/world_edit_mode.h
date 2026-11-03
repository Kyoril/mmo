// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "base/filesystem.h"
#include "game_common/fog_volume.h"
#include "scene_graph/manual_render_object.h"
#include "scene_graph/scene_node.h"
#include "terrain_io/water_flow.h"

#include <memory>
#include <vector>

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
	class InstancedFoliage;
	class Selectable;

	namespace pcg
	{
		struct Placement;
	}

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

		/// @brief Adds an object to the editor selection (the transform widget then acts on it). Does not
		///        clear the selection first; call ClearSelection() for an exclusive selection.
		virtual void AddToSelection(std::unique_ptr<Selectable> selectable) = 0;

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

		/// @brief Moves the camera to focus on a world position (e.g. a place picked from a list).
		/// @param position The world position to focus.
		virtual void FocusWorldPosition(const Vector3& position) = 0;

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

		/// @brief Gets the authored local fog volumes of the currently open map, for editing.
		virtual std::vector<FogVolume>& GetFogVolumes() = 0;

		/// @brief Marks the fog volume list as changed so the next Save() rewrites (or removes)
		///        the map's `.hfog` file.
		virtual void MarkFogVolumesChanged() = 0;

		/// @brief Generates a fresh fog volume id (the current maximum id in use, plus one).
		virtual uint32 GenerateFogVolumeId() = 0;

		/// @brief Creates the editor wireframe for the fog volume with the given id.
		/// @param volumeId Id of a volume in GetFogVolumes().
		/// @param select Whether the volume becomes the active selection.
		virtual void AddFogVolumeVisual(uint32 volumeId, bool select) = 0;

		/// @brief Re-syncs a fog volume's wireframe (geometry, colour, position, yaw) with its data
		///        and lets the transform widget follow if that volume is selected.
		/// @param volumeId Id of a volume in GetFogVolumes().
		virtual void RefreshFogVolumeVisual(uint32 volumeId) = 0;

		/// @brief Destroys every fog volume wireframe. Clears the selection first.
		virtual void RemoveAllFogVolumeVisuals() = 0;

		/// @brief Makes the fog volume with the given id the active selection.
		/// @param volumeId Id of a volume that has a visual.
		virtual void SelectFogVolume(uint32 volumeId) = 0;

		/// @brief Deletes a fog volume and its wireframe, and flags the fog volumes as changed.
		/// @param volumeId Id of the volume to delete.
		virtual void RemoveFogVolume(uint32 volumeId) = 0;

		/// @brief Gets the id of the currently selected fog volume, or 0 if none is selected.
		virtual uint32 GetSelectedFogVolumeId() const = 0;

		/// @brief Offers a mesh dropped into the viewport to the active prop kit of the selected building.
		/// Only a drop point inside the building's world bounds goes into the kit; a drop anywhere else
		/// is declined so it creates an ordinary world entity.
		/// @param meshPath The mesh asset (.hmsh).
		/// @param worldPosition The drop point in world space.
		/// @return true if the mesh was added to a kit, in which case no world entity must be created.
		virtual bool AddMeshToActiveKit(const String& meshPath, const Vector3& worldPosition) { return false; }

		/// @brief Creates the map entity of a generated (PCG) placement, marked modified so the next
		///        Save() writes it. PlacementKind::WorldModel, and Entity placements whose asset is a
		///        .hwmo, become a world model with the placement's prop kits applied in order (the
		///        paths are kept even if a kit fails to load). Other Entity placements become a mesh
		///        entity with the placement's material overrides applied (overrides for a slot the
		///        mesh does not have are skipped with a warning). Foliage placements are no map
		///        entities and are refused.
		/// @param placement The placement (asset, transform, kits, material overrides).
		/// @param uniqueId Unique id of the new entity (from GenerateEntityUniqueId).
		/// @return True if the entity was created.
		virtual bool CreatePlacementEntity(const pcg::Placement& placement, uint64 uniqueId) = 0;

		/// @brief Removes a loaded map entity (mesh or world model) by its unique id, exactly like
		///        deleting it by hand: its file is deleted on the next Save() and it leaves the selection.
		/// @param uniqueId The unique id of the entity.
		/// @return False if no loaded entity has that id.
		virtual bool RemoveMapEntityById(uint64 uniqueId) = 0;

		/// @brief Whether a map entity with the given unique id is currently loaded.
		/// @param uniqueId The unique id of the entity.
		virtual bool HasMapEntity(uint64 uniqueId) const = 0;

		/// @brief Sets the display name and category of a loaded map entity and marks it modified.
		/// @param uniqueId The unique id of the entity. Unknown ids are ignored.
		/// @param name The display name.
		/// @param category The category.
		virtual void SetMapEntityInfo(uint64 uniqueId, const String& name, const String& category) = 0;

		/// @brief Moves a loaded map entity to a new world transform and marks it modified, like the
		///        transform widget does. The unique id (and thus the entity file) is kept.
		/// @param uniqueId The unique id of the entity.
		/// @param position World position.
		/// @param orientation World orientation.
		/// @param scale World scale.
		/// @return False if no loaded entity has that id.
		virtual bool SetMapEntityTransform(uint64 uniqueId, const Vector3& position, const Quaternion& orientation, const Vector3& scale) = 0;

		/// @brief Generates a fresh unique id for a map entity (the source CreateMapEntity uses for 0).
		virtual uint64 GenerateEntityUniqueId() = 0;

		/// @brief Gets the world's authored instanced foliage (trees, bushes).
		virtual InstancedFoliage& GetInstancedFoliage() = 0;

		/// @brief Marks a terrain page's authored foliage as changed so the next Save() rewrites
		///        its .hfol file (forwards to the foliage edit mode, which owns the dirty set).
		/// @param pageIndex The page index as FoliageEditMode::PageIndexFromPosition computes it.
		virtual void MarkFoliagePageDirty(uint16 pageIndex) = 0;

		/// @brief Generates a fresh unique id for a foliage instance (the source the foliage brush uses).
		virtual uint64 GenerateFoliageUniqueId() = 0;

		/// @brief Loads, synchronously, every terrain page intersecting the world rect (prepare +
		///        load) together with its entities and authored foliage, so content there can be
		///        edited and found by id. Pages outside the terrain are skipped.
		/// @param minX Minimum world X.
		/// @param minZ Minimum world Z.
		/// @param maxX Maximum world X.
		/// @param maxZ Maximum world Z.
		/// @param error Receives the pages that are not ready when the call fails.
		/// @return True only if every page intersecting the rect (inside the terrain) is prepared and
		///         loaded afterwards. Callers must not edit the terrain there otherwise.
		virtual bool EnsurePagesLoaded(float minX, float minZ, float maxX, float maxZ, String& error) = 0;

		/// @brief Gets a counter that changes on every terrain height, splat, hole, water or tile material
		///        edit (and on sea level changes), whatever the edit path. Pages streaming in or out do not
		///        change it. Hooked at Page::MarkChanged through terrain::Terrain::GetDataRevision.
		/// @return The revision, or 0 if the world has no terrain.
		virtual uint64 GetTerrainRevision() const = 0;

		/// @brief Asset path of the world's hand-drawn river paths (.hflow next to the world file), read with
		///        ReadFlowPathsFile.
		[[nodiscard]] virtual String GetFlowPathsFileName() const = 0;

		/// @brief The hand-drawn river paths and flow settings the open editor holds, as they are on the terrain pages
		///        now (saved or not; a pending deferred flow apply is carried out first).
		/// @param paths Receives the usable hand paths (two points or more), in river order.
		/// @param settings Receives the world's flow settings.
		/// @return False if the editor holds no rivers (no Water mode); the caller then reads GetFlowPathsFileName().
		virtual bool GetHandFlowPaths(std::vector<terrain_io::water_flow::FlowPath>& paths, terrain_io::water_flow::FlowSettings& settings) = 0;
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

		/// @brief Called once per frame while the mode is active.
		virtual void OnUpdate(float deltaSeconds) {}

		/// @brief Called on any frame where OnMouseHold is not called, so a mode that treats a
		///        held drag as a continuous stroke can tell a pause apart from a fast movement.
		///        Without it, leaving the viewport mid-drag and re-entering elsewhere looks
		///        identical to the cursor having travelled between the two points.
		virtual void OnStrokeInterrupted() {}

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
