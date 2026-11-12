// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include <functional>
#include <set>
#include <imgui.h>
#include <thread>
#include <asio/io_service.hpp>
#include "detour_debug_drawer.h"
#include "selected_map_entity.h"
#include "paging/world_page_loader.h"
#include "base/id_generator.h"
#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "paging/loaded_page_section.h"
#include "paging/page_loader_listener.h"
#include "paging/page_pov_partitioner.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/world_grid.h"
#include "scene_graph/world_model_instance.h"
#include "scene_graph/prop_kit.h"
#include "base/utilities.h"
#include "transform_widget.h"
#include "editors/mesh_editor/collision_overlay.h"
#include "selection.h"
#include "selection_raycaster.h"
#include "nav_mesh/map.h"
#include "terrain/terrain.h"
#include "terrain_lod_baker.h"
#include "terrain/far_terrain.h"
#include "editor_viewpoint.h"
#include <algorithm>
#include <filesystem>
#include "edit_modes/terrain_edit_mode.h"
#include "edit_modes/spawn_edit_mode.h"
#include "edit_modes/entity_edit_mode.h"
#include "scene_graph/octree_scene.h"
#include "graphics/sky_component.h"
#include "scene_graph/environment_controller.h"
#include "scene_graph/wind_simulation.h"
#include "scene_graph/environment_profile_proto.h"

#include "deferred_shading/deferred_renderer.h"
#include "deferred_shading/water_volume_system.h"
#include "deferred_shading/ocean_feed.h"
#include "deferred_shading/ocean_profile.h"
#include "terrain/ocean_mask.h"
#include "edit_modes/navigation_edit_mode.h"
#include "edit_modes/sky_edit_mode.h"
#include "edit_modes/area_trigger_edit_mode.h"
#include "edit_modes/fog_volume_edit_mode.h"
#include "edit_modes/atlas_edit_mode.h"
#include "edit_modes/water_edit_mode.h"
#include "edit_modes/foliage_edit_mode.h"
#include "edit_modes/procedural_edit_mode.h"
#include "scene_graph/instanced_foliage.h"
#include "scene_graph/foliage.h"
#include "scene_outline_window.h"

#include <map>
#include <tuple>
#include <vector>
#include "grid_snap_settings.h"
#include "details_panel.h"
#include "world_settings_panel.h"
#include "viewport_panel.h"
#include "spawn_palette_panel.h"
#include "spawn_list_panel.h"
#include "entity_factory.h"
#include "minimap_baker.h"
#include "minimap_grid_view.h"
#include "pcg/graph/debug_geometry.h"

namespace mmo
{
	namespace proto
	{
		class MapEntry;
	}

	class WorldEditor;
	class Entity;
	class Camera;
	class SceneNode;
	class InstancedFoliage;
	class Foliage;

	/// @brief Represents the type of a map entity.
	enum class MapEntityType
	{
		/// @brief A regular mesh entity.
		Mesh,
		/// @brief A world model object instance.
		WorldModel
	};

	/// @brief Represents a single entity on the map (mesh or world model).
	class MapEntity final : public NonCopyable
	{
	public:
		typedef signal<void(const MapEntity &)> TransformChangedSignal;
		TransformChangedSignal transformChanged;

		typedef signal<void(MapEntity &)> RemoveSignal;
		RemoveSignal remove;

	public:
		/// @brief Constructs a mesh-based map entity.
		explicit MapEntity(Scene &scene, SceneNode &sceneNode, Entity &entity, uint64 uniqueId)
			: m_scene(scene)
			, m_sceneNode(sceneNode)
			, m_entity(&entity)
			, m_worldModelInstance(nullptr)
			, m_uniqueId(uniqueId)
			, m_entityType(MapEntityType::Mesh)
		{
		}

		/// @brief Constructs a world model-based map entity.
		explicit MapEntity(Scene &scene, SceneNode &sceneNode, WorldModelInstance &worldModelInstance, uint64 uniqueId)
			: m_scene(scene)
			, m_sceneNode(sceneNode)
			, m_entity(nullptr)
			, m_worldModelInstance(&worldModelInstance)
			, m_uniqueId(uniqueId)
			, m_entityType(MapEntityType::WorldModel)
		{
		}

		~MapEntity() override
		{
			if (m_entity)
			{
				m_scene.DestroyEntity(*m_entity);
			}
			if (m_worldModelInstance)
			{
				// Destroy all child scene objects (group geometry, doodads, lights)
				// before detaching from the scene node, while the scene is still accessible.
				m_worldModelInstance->Destroy(m_scene);
				m_sceneNode.DetachObject(*m_worldModelInstance);
				// World model instance will be deleted when the unique_ptr is destroyed
			}
			m_scene.DestroySceneNode(m_sceneNode);
		}

	public:
		/// @brief Gets the type of this map entity.
		/// @return The entity type (Mesh or WorldModel).
		MapEntityType GetEntityType() const
		{
			return m_entityType;
		}

		/// @brief Checks if this is a mesh entity.
		/// @return True if this is a mesh entity.
		bool IsMeshEntity() const
		{
			return m_entityType == MapEntityType::Mesh;
		}

		/// @brief Checks if this is a world model entity.
		/// @return True if this is a world model entity.
		bool IsWorldModelEntity() const
		{
			return m_entityType == MapEntityType::WorldModel;
		}

		SceneNode &GetSceneNode() const
		{
			return m_sceneNode;
		}

		/// @brief Gets the mesh entity if this is a mesh-based map entity.
		/// @return Pointer to the entity, or nullptr if this is a world model entity.
		Entity *GetEntity() const
		{
			return m_entity;
		}

		/// @brief Gets the world model instance if this is a world model-based map entity.
		/// @return Pointer to the world model instance, or nullptr if this is a mesh entity.
		WorldModelInstance *GetWorldModelInstance() const
		{
			return m_worldModelInstance;
		}

		/// @brief Gets the movable object (works for both entity types).
		/// @return Pointer to the movable object.
		MovableObject *GetMovableObject() const
		{
			if (m_entity)
			{
				return m_entity;
			}
			return m_worldModelInstance;
		}

		/// @brief Gets the asset name for this entity.
		/// @return The asset name (mesh name or world model name).
		String GetAssetName() const;

		uint64 GetUniqueId() const
		{
			return m_uniqueId;
		}

		void ClearReferencePosition()
		{
			m_referencePagePosition.reset();
		}

		void SetReferencePagePosition(const PagePosition &position)
		{
			m_referencePagePosition = position;
		}

		const std::optional<PagePosition> &GetReferencePagePosition() const
		{
			return m_referencePagePosition;
		}

		bool IsModified() const
		{
			return m_modified;
		}

		void MarkModified()
		{
			m_modified = true;
		}

		void MarkAsUnmodified()
		{
			m_modified = false;
		}

		const String &GetDisplayName() const
		{
			return m_name;
		}

		const String &GetCategory() const
		{
			return m_category;
		}

		void SetDisplayName(const String &name)
		{
			m_name = name;
			m_modified = true;
		}

		void SetCategory(const String &category)
		{
			m_category = category;
			m_modified = true;
		}

		/// @brief Sets the asset name for this entity.
		/// @param assetName The asset name to set.
		void SetAssetName(const String &assetName)
		{
			m_assetName = assetName;
		}

		/// @brief Gets the ordered prop kit asset paths of this entity (world models only).
		const std::vector<String> &GetKitPaths() const
		{
			return m_kitPaths;
		}

		/// @brief Replaces the prop kit asset paths and marks the entity as modified.
		/// @param kitPaths The new ordered list of prop kit asset paths.
		void SetKitPaths(std::vector<String> kitPaths)
		{
			m_kitPaths = std::move(kitPaths);
			m_modified = true;
		}

	private:
		Scene &m_scene;
		SceneNode &m_sceneNode;
		Entity *m_entity;
		WorldModelInstance *m_worldModelInstance;
		uint64 m_uniqueId;
		MapEntityType m_entityType;
		std::optional<PagePosition> m_referencePagePosition;
		bool m_modified{false};
		String m_name;
		String m_category;
		String m_assetName;  ///< Cached asset name for world model entities
		std::vector<String> m_kitPaths;  ///< Ordered prop kit asset paths (world model entities)
	};

	struct WorldPage
	{
	};

	class SelectedKitProp;
	class SelectedSplinePoint;
	class SelectedControlTransform;
	class SectionPanel;
	class PcgDebugLayer;

	class SelectableVisitor
	{
	public:
		virtual ~SelectableVisitor() = default;

		virtual void Visit(SelectedMapEntity &selectable) = 0;

		/// @brief Visits a prop of a prop kit applied to a placed world model.
		virtual void Visit(SelectedKitProp &selectable) = 0;

		virtual void Visit(SelectedTerrainTile &selectable) = 0;

		virtual void Visit(SelectedUnitSpawn &selectable) = 0;

		virtual void Visit(SelectedObjectSpawn &selectable) = 0;

		virtual void Visit(SelectedAreaTrigger &selectable) = 0;

		virtual void Visit(SelectedFogVolume &selectable) = 0;

		/// @brief Visits a control point of an editor spline (e.g. an oasis loop).
		virtual void Visit(SelectedSplinePoint &selectable) = 0;

		/// @brief Visits the transform of a placed procedural volume or point control.
		virtual void Visit(SelectedControlTransform &selectable) = 0;
	};

	class WorldEditorInstance final : public EditorInstance, public IPageLoaderListener, public SelectableVisitor, public ChunkReader, public IWorldEditor
	{
	public:
		explicit WorldEditorInstance(EditorHost &host, WorldEditor &editor, Path asset);
		~WorldEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;

		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

		bool Save() override;

		/// Asset path of the world's river flow paths (.hflow next to the world file).
		[[nodiscard]] String GetFlowPathsFileName() const override;

		/// Hand rivers and flow settings of Water mode as they are on the pages (see IWorldEditor::GetHandFlowPaths).
		bool GetHandFlowPaths(std::vector<terrain_io::water_flow::FlowPath>& paths, terrain_io::water_flow::FlowSettings& settings) override;

		/// @brief Asset path of the folder holding this world's procedural records (.hpcg).
		[[nodiscard]] String GetPcgFolderName() const;

	private:
		// UI related methods
		void HandleKeyboardShortcuts();
		void DrawSceneOutlinePanel(const String &sceneOutlineId);
		void InitializeDockLayout(ImGuiID dockspaceId, const String &viewportId, const String &detailsId, const String &worldSettingsId, const String &spawnPaletteId, const String &spawnListId);

		void OnTerrainMouseMoved(float viewportX, float viewportY);

		Entity *CreateMapEntity(const String &assetName, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale, uint64 objectId) override;

		WorldModelInstance *CreateWorldModelEntity(const String &assetName, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale, uint64 objectId) override;

		void OnMapEntityRemoved(MapEntity &entity);

		PagePosition GetPagePositionFromCamera() const;

		void SetMapEntry(proto::MapEntry *entry);

		/// @brief Advances the environment blend and applies it to the sky, lights and renderer.
		void UpdateEnvironment(float deltaSeconds);

	public:
		/// @brief Creates a duplication callback for the given map entity.
		/// The returned function, when invoked with the current selectable transform,
		/// spawns a new copy of the entity at that transform.
		/// @param entity The entity to duplicate.
		/// @return A callable that creates a copy of entity when invoked.
		std::function<void(Selectable&)> MakeDuplicationCallback(MapEntity& entity);

		void OnPageAvailabilityChanged(const PageNeighborhood &page, bool isAvailable) override;

		void EnsurePageIsLoaded(PagePosition pos);

		void Visit(SelectedMapEntity &selectable) override;

		/// @brief Draws the details of a selected kit prop (shared mesh reference properties).
		void Visit(SelectedKitProp &selectable) override;

		void Visit(SelectedTerrainTile &selectable) override;

		void Visit(SelectedUnitSpawn &selectable) override;

		void Visit(SelectedObjectSpawn &selectable) override;

		void Visit(SelectedAreaTrigger &selectable) override;

		void Visit(SelectedFogVolume &selectable) override;

		/// @brief Draws the details of a selected spline point.
		void Visit(SelectedSplinePoint &selectable) override;

		void Visit(SelectedControlTransform &selectable) override;

	private:
		/// Draws the weighted unit entry table of a mixed unit spawn (UnitSpawnEntry::unitentries).
		void DrawMixedUnitSpawnEntries(SelectedUnitSpawn &selectable);

		bool ReadMVERChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMeshChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadEntityChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadEntityChunkV2(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadTerrainChunk(io::Reader &reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnReadFinished() override;

		void SetEditMode(WorldEditMode *editMode);

	public:
		void ClearSelection() override;

		/// @copydoc IWorldEditor::AddToSelection
		void AddToSelection(std::unique_ptr<Selectable> selectable) override;

		void RemoveAllUnitSpawns() override;

		void AddUnitSpawn(proto::UnitSpawnEntry &spawn, bool select) override;

		void AddObjectSpawn(proto::ObjectSpawnEntry &spawn) override;

		void RemoveUnitSpawn(const proto::UnitSpawnEntry& spawn) override;

		void RemoveObjectSpawn(const proto::ObjectSpawnEntry& spawn) override;

		void SelectUnitSpawn(proto::UnitSpawnEntry& spawn) override;

		void SelectObjectSpawn(proto::ObjectSpawnEntry& spawn) override;

		const void* GetSelectedSpawnEntry() const override;

		void FocusSelection() override;

		/// @copydoc IWorldEditor::FocusWorldPosition
		void FocusWorldPosition(const Vector3& position) override;

		void AddAreaTrigger(proto::AreaTriggerEntry &trigger, bool select = true) override;

		void RemoveAllAreaTriggers() override;

		proto::AreaTriggerEntry* FindAreaTriggerByRenderObject(ManualRenderObject* renderObject);

		void SelectAreaTrigger(proto::AreaTriggerEntry& trigger);

		Camera &GetCamera() const override;

		bool IsGridSnapEnabled() const override { return m_gridSnapSettings.IsEnabled(); }

		float GetTranslateGridSnapSize() const override { return m_gridSnapSettings.GetCurrentTranslateSnap(); }

		bool HasTerrain() const override { return m_hasTerrain; }

		terrain::Terrain *GetTerrain() const override { return m_terrain.get(); }

		bool IsTransforming() const override;

		ManualRenderObject* CreateManualRenderObject(const String& name) override;
		SceneNode* CreateChildSceneNode() override;
		void DestroyManualRenderObject(const ManualRenderObject& obj) override;
		void DestroySceneNode(const SceneNode& node) override;

		std::vector<FogVolume>& GetFogVolumes() override { return m_fogVolumes; }
		void MarkFogVolumesChanged() override { m_fogVolumesDirty = true; }
		uint32 GenerateFogVolumeId() override;

		/// @brief Creates the wireframe scene node for one fog volume, drawn in local space on a node
		///        placed at the volume's position with orientation Quaternion(Degree(yaw), UnitY).
		void AddFogVolumeVisual(uint32 volumeId, bool select) override;

		void RefreshFogVolumeVisual(uint32 volumeId) override;

		void RemoveAllFogVolumeVisuals() override;

		void SelectFogVolume(uint32 volumeId) override;

		void RemoveFogVolume(uint32 volumeId) override;

		uint32 GetSelectedFogVolumeId() const override;

		/// @copydoc IWorldEditor::AddMeshToActiveKit
		bool AddMeshToActiveKit(const String &meshPath, const Vector3 &worldPosition) override;

		/// @copydoc IWorldEditor::CreatePlacementEntity
		bool CreatePlacementEntity(const pcg::Placement &placement, uint64 uniqueId) override;

		/// @copydoc IWorldEditor::RemoveMapEntityById
		bool RemoveMapEntityById(uint64 uniqueId) override;

		/// @copydoc IWorldEditor::HasMapEntity
		bool HasMapEntity(uint64 uniqueId) const override;

		/// @copydoc IWorldEditor::SetMapEntityInfo
		void SetMapEntityInfo(uint64 uniqueId, const String &name, const String &category) override;

		/// @copydoc IWorldEditor::SetMapEntityTransform
		bool SetMapEntityTransform(uint64 uniqueId, const Vector3 &position, const Quaternion &orientation, const Vector3 &scale) override;

		/// @copydoc IWorldEditor::GenerateEntityUniqueId
		uint64 GenerateEntityUniqueId() override;

		/// @copydoc IWorldEditor::GetInstancedFoliage
		InstancedFoliage &GetInstancedFoliage() override { return *m_foliage; }

		/// @copydoc IWorldEditor::MarkFoliagePageDirty
		void MarkFoliagePageDirty(uint16 pageIndex) override;

		/// @copydoc IWorldEditor::GenerateFoliageUniqueId
		uint64 GenerateFoliageUniqueId() override;

		/// @copydoc IWorldEditor::EnsurePagesLoaded
		bool EnsurePagesLoaded(float minX, float minZ, float maxX, float maxZ, String &error) override;

		/// @copydoc IWorldEditor::GetTerrainRevision
		uint64 GetTerrainRevision() const override;

	public:
		// In-place furnishing: editing prop kits of placed world models

		/// @brief Finds a loaded map entity by its unique id.
		/// @param uniqueId The unique id of the entity.
		/// @return The entity, or nullptr if no loaded entity has that id.
		MapEntity *FindMapEntity(uint64 uniqueId) const;

		/// @brief Unique ids of the selected map entities (meshes and world models), in selection order.
		/// @param out Cleared, then receives the ids.
		void GetSelectedMapEntityIds(std::vector<uint64> &out) const;

		/// @brief Gets a prop of a kit as the world editor currently sees it: the unsaved working copy
		///        if the kit was edited here, otherwise the cached kit.
		/// @param kitPath Asset path of the kit.
		/// @param propIndex Index of the prop in the kit.
		/// @return The prop, or nullptr if the kit is not loaded or the index is out of range. Only valid
		///         until the kit is edited again.
		const WorldModelMeshRef *FindKitProp(const String &kitPath, size_t propIndex) const;

		/// @brief Edits one prop of a kit. The kit becomes an unsaved working copy (dirty) and every
		///        loaded placement using it is refreshed.
		/// @param placementId Id of the placement the edit was made on (counted out of the save prompt).
		/// @param kitPath Asset path of the kit.
		/// @param propIndex Index of the prop in the kit.
		/// @param edit Callback applying the change to the prop.
		void EditKitProp(uint64 placementId, const String &kitPath, size_t propIndex, const std::function<void(WorldModelMeshRef &)> &edit);

		/// @brief Appends a copy of a kit prop to the same kit (dirty, refreshed like an edit).
		/// @param placementId Id of the placement the edit was made on.
		/// @param kitPath Asset path of the kit.
		/// @param propIndex Index of the prop to copy.
		void DuplicateKitProp(uint64 placementId, const String &kitPath, size_t propIndex);

		/// @brief Queues a kit prop for removal. Removals are applied once per frame after the UI ran,
		///        highest index first, so several props of one kit can be removed together.
		/// @param placementId Id of the placement the edit was made on.
		/// @param kitPath Asset path of the kit.
		/// @param propIndex Index of the prop to remove.
		void QueueKitPropRemoval(uint64 placementId, const String &kitPath, size_t propIndex);

	private:
		/// @brief Gets a kit for display: the dirty working copy if there is one, otherwise the cached
		///        kit. Never loads.
		PropKitPtr ResolveKit(const String &kitPath) const;

		/// @brief Gets a kit for applying: the dirty working copy if there is one, otherwise the kit is
		///        loaded through the PropKitManager and remembered so later saves are noticed.
		PropKitPtr LoadKit(const String &kitPath);

		/// @brief Gets the working copy of a kit for editing, creating it from the cached kit first.
		/// @return The working copy, or nullptr if the kit cannot be loaded.
		PropKit *GetEditableKit(const String &kitPath, uint64 originPlacementId);

		/// @brief Rebuilds a placement's kit content from its kit list (SetPropKits).
		void ApplyKits(MapEntity &placement);

		/// @brief Calls ApplyKits on every loaded placement whose kit list contains the kit.
		void RefreshKitUsers(const String &kitPath);

		/// @brief Counts the loaded placements whose kit list contains the kit.
		/// @param kitPath Asset path of the kit.
		/// @param excludePlacementId Placement not to count (0 counts all).
		size_t CountKitUsers(const String &kitPath, uint64 excludePlacementId) const;

		/// @brief Gets the active kit of a placement (where dropped meshes go). Defaults to the first kit
		///        of its list; empty when the placement has no kits or "None" was picked.
		String GetActiveKit(const MapEntity &placement) const;

		/// @brief Gets the building the current selection belongs to: a selected world model placement,
		///        or the placement of a selected kit prop.
		MapEntity *GetSelectedPlacement() const;

		/// @brief Picks a kit prop of the selected building under the cursor and selects it.
		/// @return true if a kit prop was selected.
		bool TrySelectKitProp(float viewportX, float viewportY, bool allowMultiSelect);

		/// @brief Shows the bounds of the most recently selected kit prop, if it is one.
		void UpdateSelectedKitPropBounds();

		/// @brief Shows, hides or rebuilds the kit collision display of the selected building as needed.
		///        The display belongs to one building and switches off when the selection moves on.
		void UpdateKitCollisionOverlay();

		/// @brief Rebuilds the kit collision display from the kit props of a placement, in world space.
		void RebuildKitCollisionOverlay(const MapEntity &placement);

		/// @brief Deselects the selected kit props of a placement whose kit is no longer in its kit list.
		/// @param placement The placement whose kit list just changed.
		void DeselectRemovedKitProps(const MapEntity &placement);

		/// @brief Notices kits that were replaced in the PropKitManager cache by someone else (the prop
		///        kit editor's save) and refreshes the placements using them.
		void PollKitCache();

		/// @brief Applies queued kit prop removals.
		void ApplyPendingKitPropRemovals();

		/// @brief Writes a prop kit file.
		static bool WriteKitFile(const String &kitPath, const PropKit &kit);

		/// @brief Writes every dirty kit, or queues it behind the shared-kit prompt when other loaded
		///        placements use it.
		void SaveDirtyKits();

		/// @brief Writes one dirty kit, updates the cache and refreshes its users.
		void SaveDirtyKit(const String &kitPath);

		/// @brief Writes the active kit of a placement to a new file and swaps it into the placement's list.
		void SaveKitAsNew(uint64 placementId, const String &sourceKitPath, const String &targetKitPath);

		/// @brief Draws the shared-kit save prompt and the "Save as new kit" dialog.
		void DrawKitModals();

	private:
		/// @brief Returns the selected fog volume selectable, or nullptr if the selection is empty or its
		///        last-added entry is not a fog volume. With a mixed selection (e.g. a fog volume plus a
		///        map entity picked via ctrl-click in the scene outline) this only reports the fog volume
		///        when it is the most recently added entry; use SelectionHasFogVolume() to check whether
		///        a fog volume is present anywhere in the selection regardless of order.
		SelectedFogVolume* GetSelectedFogVolume() const;

		/// @brief True if the current selection contains a SelectedFogVolume anywhere (not just as the
		///        last entry). When volumeId is non-null, only a fog volume with that id counts.
		///        Used to guard against destroying a fog volume's visual while a SelectedFogVolume
		///        referencing it is still part of a mixed selection.
		bool SelectionHasFogVolume(const uint32* volumeId) const;

		/// @brief Destroys the wireframe of one fog volume, if it has one.
		void RemoveFogVolumeVisual(uint32 volumeId);

		/// @brief Destroys a fog volume's wireframe and erases it from m_fogVolumes, without touching the
		///        selection. Used as the selectable's removal callback, which runs while the selection is
		///        being iterated.
		void EraseFogVolume(uint32 volumeId);

		uint16 BuildPageIndex(uint8 x, uint8 y) const;

		bool GetPageCoordinatesFromIndex(uint16 pageIndex, uint8 &x, uint8 &y) const;

		void LoadPageEntities(uint8 x, uint8 y);

		void UnloadPageEntities(uint8 x, uint8 y);

		/// @brief Makes sure the entities of the pages around a page are loaded, for the minimap bake,
		///        and unloads the ones a previous call loaded that are no longer around it.
		/// @return The scene nodes of every loaded world model (WMO) entity.
		std::vector<const SceneNode*> PrepareMinimapBakeEntities(uint32 x, uint32 z);

		/// @brief Unloads every page's entities PrepareMinimapBakeEntities loaded.
		void ReleaseMinimapBakeEntities();

		/// @brief Loads the authored foliage (.hfol) of a terrain page into the editor's foliage renderer.
		/// @param x Page X coordinate.
		/// @param y Page Y coordinate.
		void LoadPageFoliage(uint8 x, uint8 y);

		/// @brief Creates and configures the procedural grass system to mirror the game client.
		void SetupGrass();

		/// @brief Registers procedural foliage layers from the tile materials of a loaded terrain page.
		void RegisterPageFoliage(uint32 pageX, uint32 pageY);

		/// @brief Ref-decrements (and removes when unused) the foliage layers a terrain page contributed.
		void UnregisterPageFoliage(uint32 pageX, uint32 pageY);

		/// @brief Recomputes and applies the effective water visibility.
		/// @details The water surface is shown when the "Show Water" setting is enabled, or whenever the
		///          water edit mode is active (so water can always be edited), whichever applies.
		void UpdateWaterVisibility();

	public:
		const std::filesystem::path GetWorldPath() const override { return m_assetPath; }

		/// @brief Gets the deferred renderer.
		/// @return Pointer to the deferred renderer, or nullptr if not available.
		DeferredRenderer* GetDeferredRenderer() const { return m_deferredRenderer.get(); }

		/// @brief Bakes the minimap tiles of every terrain page (see MinimapBaker) into
		///        Textures/Minimaps/<world>/<(x << 8) | z>.htex.
		/// @param onlyStale Skip pages whose tile is newer than their .tile file.
		/// @param region Only pages inside this inclusive page rectangle are baked.
		/// @return False when any page failed.
		bool GenerateMinimaps(bool onlyStale = false, const MinimapBaker::Region& region = MinimapBaker::Region{});

		/// @brief Bakes the world map (see WorldMapBaker) into Textures/WorldMap/<world>/ from the zones of
		///        every page and the minimap tiles. Minimap bakes run it when they finish.
		/// @return False when the world has no terrain or a file could not be written.
		bool GenerateWorldMap();

		/// @brief Starts an interactive minimap bake that runs a few pages per frame (see TickMinimapBake),
		///        so the editor stays responsive and the bake can be cancelled.
		/// @param onlyStale Skip pages whose tile is newer than their .tile file.
		void StartMinimapBake(bool onlyStale);

		/// @brief Bakes pages of the running interactive minimap bake for up to a frame budget and
		///        finishes it when the queue is empty.
		void TickMinimapBake();

		/// @brief Stops the running interactive minimap bake, keeping the tiles baked so far.
		void FinishMinimapBake();

		/// @brief Draws the "Generate Minimaps" dialog: choice of pages, then progress and Cancel.
		void DrawMinimapBakeDialog();

		/// @brief Bakes the distant-terrain data (.tlod + colour texture) the client draws beyond its
		///        streamed pages. See TerrainLodBaker.
		/// @param onlyStale Skip pages whose baked files are newer than their .tile and that have no unsaved changes.
		/// @return false if there is no terrain or any page failed to bake.
		bool GenerateTerrainLod(bool onlyStale);

		/// @brief The most recently created world editor instance (the --bake-terrain-lod command line
		///        job bakes the world it just opened). Null once that instance is gone.
		static WorldEditorInstance* GetLastCreated() { return s_lastCreated; }

		/// @brief Every world editor instance that is currently open, in creation order. The constructor
		///        adds the instance, the destructor removes it. Main thread only; do not hold on to the
		///        pointers across frames, keep GetInstanceSerial and use FindOpenInstance instead.
		static const std::vector<WorldEditorInstance*>& GetOpenInstances() { return s_openInstances; }

		/// @brief Finds an open instance by its serial.
		/// @param serial A value from GetInstanceSerial.
		/// @return The instance, or null if it has been closed (or the serial is 0).
		static WorldEditorInstance* FindOpenInstance(uint64 serial);

		/// @brief Gets the serial of this instance: unique per process and never reused, so it stays a
		///        safe handle after the instance has been closed (FindOpenInstance then returns null).
		[[nodiscard]] uint64 GetInstanceSerial() const { return m_instanceSerial; }

		/// @brief Rebuilds procedural content (oases, section paths and graph instances) without any UI, for
		///        unattended runs.
		/// @param idOrZero Id of the control to rebuild, or 0 for all of them.
		/// @param error Receives the reason when the call returns false.
		/// @param background Rebuild a graph instance the way the editor does in the background (worker thread, written
		///        on an update), and wait for it (see ProceduralEditMode::RebuildInBackgroundAndWait).
		/// @return True if everything requested was rebuilt.
		bool RebuildProcedural(uint64 idOrZero, String& error, bool background = false);

		/// @brief Adds an oasis like the "New Oasis" button does, centred on (x, z), and builds it,
		///        for unattended runs. The world is not saved.
		/// @param x World X of the centre.
		/// @param z World Z of the centre.
		/// @param radius Loop radius.
		/// @param seed Seed of the oasis.
		/// @param name Name of the oasis; empty picks the default name.
		/// @param id Receives the id of the new oasis (also when only the build failed).
		/// @param error Receives the reason when the call returns false.
		/// @return True if the oasis was added and built.
		bool CreateProceduralOasis(float x, float z, float radius, uint64 seed, const String& name, uint64& id, String& error);

		/// @brief Adds a section path through the given x/z points (at terrain height) like the "New
		///        Section Path" button does, and builds it, for unattended runs. The world is not saved.
		///        Refused before anything is added if the section asset is missing or unreadable.
		/// @param points World x/z of the control points, in order (at least 2, at least 3 for a loop).
		/// @param sectionAsset Asset path of the baked section (.hsec).
		/// @param options Seed, loop flag, height mode, mass, facade variety and settings of the path.
		/// @param name Name of the path; empty picks the default name.
		/// @param id Receives the id of the new path (also when only the build failed).
		/// @param error Receives the reason when the call returns false.
		/// @return True if the path was added and built.
		bool CreateProceduralSectionPath(const std::vector<std::pair<float, float>>& points, const String& sectionAsset,
			const NewSectionPathOptions& options, const String& name, uint64& id, String& error);

		/// @brief Adds a placed control of a control type through the given x/z points (at terrain height)
		///        and sets property overrides, for unattended runs. The world is not saved. Refused before
		///        anything is added if the type is missing or invalid, the points do not fit its shape, or a
		///        setting names no property of the type or has an invalid value.
		/// @param typePath Asset path of the control type (.hctl).
		/// @param points World x/z of the points: a loop needs at least 3, a spline at least 2, a volume or
		///        point exactly one (its centre).
		/// @param name Name of the control; empty picks the default name.
		/// @param settings Property overrides as name and value text, applied in order.
		/// @param id Receives the id of the new control.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the control was added.
		bool CreateProceduralControl(const String& typePath, const std::vector<std::pair<float, float>>& points, const String& name,
			const std::vector<std::pair<String, String>>& settings, uint64& id, String& error);

		/// @brief Adds a graph instance over a closed area through the given x/z points, for unattended runs. Does
		///        not build it and does not save the world. Refused before anything is added if the area has fewer
		///        than 3 points, overlaps another graph instance's area, the graph cannot be read, or a parameter
		///        setting names no parameter of the graph or has an invalid value.
		/// @param graphPath Asset path of the graph (.hpcgg).
		/// @param points World x/z of the area points, at least 3.
		/// @param seed Seed of the instance.
		/// @param name Name of the instance; empty picks the default name.
		/// @param settings Parameter overrides as name and value text.
		/// @param id Receives the id of the new instance.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the instance was added.
		bool CreateProceduralGraphInstance(const String& graphPath, const std::vector<std::pair<float, float>>& points, uint64 seed, const String& name,
			const std::vector<std::pair<String, String>>& settings, uint64& id, String& error);

		/// @brief Evaluates a graph instance's saved graph without applying it, for unattended runs.
		/// @param id Id of the graph instance.
		/// @param out Receives the evaluation.
		/// @param error Receives the reason when the call returns false.
		/// @return False if there is no such instance or its graph or context could not be prepared.
		bool EvaluateProceduralGraphInstance(uint64 id, GraphInstanceEvaluation& out, String& error);

		/// @brief Gets the procedural edit mode of this world (placed controls, control library), or null.
		[[nodiscard]] ProceduralEditMode* GetProceduralEditMode() const { return m_proceduralEditMode.get(); }

		/// @brief Replaces the debug lines an owner (a PCG graph editor's debug view) draws into this world's
		///        viewport, creating its layer on first use. Debug layers are never saved.
		/// @param ownerKey Identifies the owner; each owner has one layer.
		/// @param lines The lines to draw (empty clears the drawing but keeps the layer).
		void SetPcgDebugLayer(uint64 ownerKey, std::vector<pcg::graph::DebugLine> lines);

		/// @brief Removes an owner's debug layer and its lines. Does nothing if the owner has none.
		/// @param ownerKey Identifies the owner.
		void ClearPcgDebugLayer(uint64 ownerKey);

		/// @brief Deletes a procedural control (oasis, section path, graph instance or placed control) and
		///        everything it generated like
		///        the "Delete" button does, for unattended runs. The world is not saved.
		/// @param id Id of the control.
		/// @param error Receives the reason when the call returns false.
		/// @return True if the control was deleted.
		bool DeleteProceduralControl(uint64 id, String& error);

		/// @brief Number of oases of the world.
		size_t GetProceduralOasisCount() const { return m_proceduralEditMode ? m_proceduralEditMode->GetOasisCount() : 0; }

		/// @brief Number of procedural controls (oases, section paths, placed controls and graph instances) of the world.
		size_t GetProceduralControlCount() const { return m_proceduralEditMode ? m_proceduralEditMode->GetControlCount() : 0; }

		/// @brief Loads the entities and authored foliage of the given pages unless streaming already
		///        did, for one-shot jobs. Pages this loads are tracked like the minimap bake's and
		///        released by ReleaseForcedPages; streaming takes a page over when it arrives.
		void ForceLoadPages(const std::vector<PagePosition>& pages);

		/// @brief Unloads every page's entities and foliage that ForceLoadPages (or the minimap bake) loaded.
		void ReleaseForcedPages();

		/// @brief Collects every loaded map entity and authored foliage instance as world-space placements:
		///        meshes with their material overrides (sorted by slot), world models with their kit paths,
		///        foliage with its collision flag. The key is the object's unique id; the result is sorted by
		///        kind, then key, so it does not depend on load order.
		void CollectWorldPlacements(std::vector<pcg::Placement>& out) const;

		/// @brief Whether this world is a section world (a .hsecdef next to the .hwld, see section_bake_job.h).
		[[nodiscard]] bool IsSectionWorld() const { return m_sectionPanel != nullptr; }

		/// @brief Whether ForceLoadPages (or the minimap bake) loaded entities or foliage that
		///        ReleaseForcedPages would unload.
		[[nodiscard]] bool HasForcedPages() const { return !m_bakeEntityPages.empty() || !m_bakeFoliagePages.empty(); }

		/// @brief The scene the world is shown in, for one-shot editor jobs that render it offscreen.
		[[nodiscard]] Scene& GetScene() { return m_scene; }

		/// @brief Shows or hides the authored instanced foliage, which keeps its own visibility flag on top
		///        of its scene objects.
		/// @return Whether it was visible before.
		bool SetFoliageVisible(bool visible);

		/// @brief Runs one queued main-thread step of the page streaming.
		/// @return Whether there was one.
		bool PumpStreaming() { return m_dispatcher.poll_one() > 0; }

		/// @brief The asset files that define a page's content: its .tile, .hfol and .wobj files,
		///        whichever exist.
		std::vector<String> GetPageInputFiles(const PagePosition& page) const;

		/// @brief Moves the editor camera so it focuses the given world location (X/Z plane). The
		///        height is sampled from the terrain when available. Used by the minimap location
		///        picker and the in-editor minimap teleport panel.
		/// @param worldX Target world X coordinate.
		/// @param worldZ Target world Z coordinate.
		void MoveCameraToWorldPosition(float worldX, float worldZ);

		/// @brief Describes where the editor camera is and where it looks, in the form SetViewpoint reads.
		[[nodiscard]] EditorViewpoint GetViewpoint() const;

		/// @brief Places the editor camera exactly at a viewpoint and applies its field of view and,
		///        if the viewpoint carries one, its time of day.
		void SetViewpoint(const EditorViewpoint& viewpoint);

		/// @brief Renders the world from a viewpoint at a given resolution and saves the frame as PNG.
		///
		/// Before rendering, it waits until the pages around the viewpoint, their trees and the
		/// distant terrain have streamed in, then renders a few warm-up frames so exposure and
		/// temporal effects settle. The viewport size and camera are restored afterwards.
		/// @param viewpoint Where to place the camera.
		/// @param width Image width in pixels.
		/// @param height Image height in pixels.
		/// @param path Destination PNG file.
		/// @param showGrass Whether procedural grass is shown in the shot.
		/// @param warmupFrames Frames rendered before the one that is saved.
		/// @return True if the image was written.
		bool CaptureViewpoint(const EditorViewpoint& viewpoint, uint32 width, uint32 height, const std::filesystem::path& path, bool showGrass, uint32 warmupFrames = 24);

		/// @brief Sets how many pages around the camera the distant terrain covers (0 turns it off).
		void SetFarTerrainRadius(uint32 radius) { m_farTerrainRadius = std::min<uint32>(radius, 16); }

		/// @brief Turns the scene's fog and atmosphere on or off, like the World Settings "Show Fog" box.
		void SetFogEnabled(const bool enabled) { m_scene.SetFogEnabled(enabled); }

		/// @brief Gets how many pages around the camera the distant terrain covers.
		[[nodiscard]] uint32 GetFarTerrainRadius() const { return m_farTerrainRadius; }

	private:
		/// @brief Draws the dockable "World Map" panel which shows the world's minimap grid and lets
		///        the user quickly teleport the camera by clicking a tile.
		void DrawMinimapPanel(const String& id);

		/// @brief Creates the distant terrain stand-ins for this world, mirroring the game client.
		void CreateFarTerrain();

		/// @brief Streams distant terrain around the camera and adapts the camera's far clip to it.
		void UpdateFarTerrain();

		/// @brief Waits until the pages around the camera and the distant terrain are loaded, pumping
		///        the main-thread dispatcher. Gives up after the timeout.
		/// @return True if everything finished loading in time.
		bool WaitForStreaming(float timeoutSeconds);

		/// @brief Draws the "View" section of the World Settings panel: distant terrain, viewpoint
		///        and screenshot controls.
		void DrawViewSettings();

	private:
		WorldEditor &m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		bool m_wireFrame;
		OctreeScene m_scene;
		SceneNode *m_cameraAnchor{nullptr};
		SceneNode *m_cameraNode{nullptr};
		Entity *m_entity{nullptr};
		Camera *m_camera{nullptr};
		std::unique_ptr<WorldGrid> m_worldGrid;
		int16 m_lastMouseX{0}, m_lastMouseY{0};
		bool m_leftButtonPressed{false};
		bool m_rightButtonPressed{false};
		bool m_initDockLayout{true};
		MeshPtr m_mesh;
		MeshEntry m_entry{};
		Vector3 m_cameraVelocity{};
		bool m_hovering{false};

		std::unique_ptr<terrain::Terrain> m_terrain;

		/// @brief Hardware-instanced renderer for authored foliage (trees), mirroring the game client.
		std::unique_ptr<InstancedFoliage> m_foliage;

		/// @brief Procedural grass/vegetation system (the dense foliage seen in the client). Hidden by default.
		std::unique_ptr<Foliage> m_grass;

		/// @brief Baked low-detail stand-ins for terrain pages beyond the streamed ones, as in the client.
		std::unique_ptr<terrain::FarTerrain> m_farTerrain;

		/// @brief Distant terrain radius in pages. The client's TerrainFarRadius default.
		uint32 m_farTerrainRadius{4};

		/// @brief Text field of the View settings, holding a viewpoint to jump to.
		String m_viewpointText;

		/// @brief Size of images captured from the View settings.
		int m_captureSize[2]{1920, 1080};

		/// Key identifying a distinct procedural foliage layer derived from terrain material data
		/// (the painted .hmat or .hmi, the bound layer index and the mesh asset).
		using FoliageLayerKey = std::tuple<const MaterialInterface*, uint8, String>;

		/// A runtime foliage layer registered from terrain material data, ref-counted by loaded pages.
		struct RegisteredFoliageLayer
		{
			FoliageLayerPtr layer;
			uint32 refCount = 0;
		};

		/// All currently registered material-driven foliage layers, keyed by FoliageLayerKey.
		std::map<FoliageLayerKey, RegisteredFoliageLayer> m_foliageRegistry;

		/// Per terrain page, the set of foliage layer keys it contributed.
		std::map<uint16, std::vector<FoliageLayerKey>> m_pageFoliageKeys;

		/// @brief WYSIWYG setting: whether procedural grass is rendered in the editor. Hidden by default.
		bool m_showFoliage{false};

		/// @brief WYSIWYG setting: whether water surfaces are rendered in the editor. Visible by default.
		bool m_showWater{true};

		/// @brief Last effective water visibility pushed to the terrain (avoids redundant per-frame updates).
		bool m_waterVisibilityApplied{true};

		GridSnapSettings m_gridSnapSettings;

		std::unique_ptr<asio::io_service::work> m_work;
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::thread m_backgroundLoader;
		std::unique_ptr<LoadedPageSection> m_visibleSection;
		std::unique_ptr<WorldPageLoader> m_pageLoader;
		std::unique_ptr<PagePOVPartitioner> m_memoryPointOfView;
		std::unique_ptr<RaySceneQuery> m_raySceneQuery;

		std::vector<SceneNode *> m_spawnNodes;
		std::vector<Entity *> m_spawnEntities;

		std::vector<SceneNode *> m_areaTriggerNodes;
		std::vector<ManualRenderObject *> m_areaTriggerRenderObjects;
		std::map<proto::AreaTriggerEntry*, std::pair<SceneNode*, ManualRenderObject*>> m_areaTriggerMap;

		/// @brief Wireframe scene node and render object per fog volume id, while the fog volume mode is active.
		std::map<uint32, std::pair<SceneNode*, ManualRenderObject*>> m_fogVolumeVisuals;

		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;
		std::unique_ptr<SelectionRaycaster> m_selectionRaycaster;
		std::unique_ptr<EntityFactory> m_entityFactory;

		ManualRenderObject *m_debugBoundingBox;

		float m_cameraSpeed{20.0f};

		std::vector<std::unique_ptr<MapEntity>> m_mapEntities;
		struct DeletedEntityRef
		{
			uint64 entityId;
			uint16 pageId;
		};
		std::vector<DeletedEntityRef> m_deletedEntities;
		ImVec2 m_lastContentRectMin{};

		std::map<uint16, WorldPage> m_pages;

		std::unique_ptr<TerrainEditMode> m_terrainEditMode;
		std::unique_ptr<EntityEditMode> m_entityEditMode;
		std::unique_ptr<SpawnEditMode> m_spawnEditMode;
		std::unique_ptr<NavigationEditMode> m_navigationEditMode;
		std::unique_ptr<SkyEditMode> m_skyEditMode;
		std::unique_ptr<AreaTriggerEditMode> m_areaTriggerEditMode;
		std::unique_ptr<FogVolumeEditMode> m_fogVolumeEditMode;
		std::unique_ptr<AtlasEditMode> m_atlasEditMode;
		std::unique_ptr<WaterEditMode> m_waterEditMode;
		std::unique_ptr<ProceduralEditMode> m_proceduralEditMode;

		/// PCG graph debug views drawing into this world, by owner key (see SetPcgDebugLayer). Never saved.
		std::map<uint64, std::unique_ptr<PcgDebugLayer>> m_pcgDebugLayers;
		std::unique_ptr<FoliageEditMode> m_foliageEditMode;

		/// Section authoring panel; only set for section worlds.
		std::unique_ptr<SectionPanel> m_sectionPanel;
		std::unique_ptr<SkyComponent> m_skyComponent;

		/// Blends environment profiles for the viewport, exactly like the client.
		EnvironmentController m_environment;

		/// Gusting wind and fog noise scroll for the viewport. Advances with real frame time even while
		/// the sky clock is paused, so drifting fog stays visible while authoring.
		WindSimulation m_wind;

		/// @brief Authored local fog volumes for the currently open map, read from its `.hfog` file.
		std::vector<FogVolume> m_fogVolumes;

		/// @brief True when m_fogVolumes has unsaved changes; Save() rewrites (or removes) the `.hfog` file.
		bool m_fogVolumesDirty = false;

		/// Runtime profiles converted from the project on first use; cleared on every preview revision.
		std::unique_ptr<EnvironmentProfileCache<proto::EnvironmentProfileManager>> m_environmentProfiles;

		/// Profile id the controller targets. UINT32_MAX = none yet.
		uint32 m_environmentProfileId = UINT32_MAX;

		/// Last EnvironmentPreview::revision this instance converted profiles for.
		uint64 m_environmentRevision = 0;

		/// Whether the last update showed the Environment Profile Editor's preview.
		bool m_environmentPreviewActive = false;

		/// Sky's normalized time of day captured just before preview took over; restored when preview ends.
		float m_timeOfDayBeforePreview = 0.5f;
		WorldEditMode *m_editMode{nullptr};

		// Spawn edit mode
		proto::MapEntry *m_mapEntry{nullptr};

		Entity *m_debugEntity{nullptr};
		SceneNode *m_debugNode{nullptr};

		bool m_hasTerrain{true};
		std::vector<String> m_meshNames;

		uint32 m_worldFileVersion;

		static inline WorldEditorInstance* s_lastCreated = nullptr;

		/// Open instances (see GetOpenInstances).
		static inline std::vector<WorldEditorInstance*> s_openInstances;

		/// Last serial handed out; serials start at 1.
		static inline uint64 s_lastInstanceSerial = 0;

		/// This instance's serial (see GetInstanceSerial).
		uint64 m_instanceSerial = 0;

		std::unique_ptr<DeferredRenderer> m_deferredRenderer;

		/// @brief Answers m_waterVolume's queries from m_terrain. Declared first so it outlives it.
		std::unique_ptr<IWaterQuery> m_waterQuery;

		/// @brief Drives the underwater post-process from the editor camera, so flying below a water
		///		   surface previews what a diving player sees, from the same water profiles.
		std::unique_ptr<WaterVolumeSystem> m_waterVolume;

		/// Feeds the FFT ocean (sea level, ocean mask, Ocean water profile) every frame.
		OceanFeed<terrain::Terrain> m_oceanFeed { static_cast<float>(terrain::ocean_mask::TexelSize) };

		/// @brief Asset name of the caustics texture last handed to the underwater pass.
		String m_underwaterCausticsTexture;

		/// @brief Recomputes camera submersion and pushes the underwater state and caustics texture
		///		   to the renderer.
		void UpdateUnderwaterPreview(float deltaSeconds);

		/// Hands the terrain's sea to the FFT ocean renderer and applies the Ocean water profile.
		void UpdateOcean();

		std::unique_ptr<DetailsPanel> m_detailsPanel;
		std::unique_ptr<WorldSettingsPanel> m_worldSettingsPanel;
		std::unique_ptr<ViewportPanel> m_viewportPanel;
		std::unique_ptr<SceneOutlineWindow> m_sceneOutlineWindow;
		std::unique_ptr<SpawnPalettePanel> m_spawnPalettePanel;
		std::unique_ptr<SpawnListPanel> m_spawnListPanel;

		/// @brief Minimap grid used by the "World Map" panel to teleport the camera.
		MinimapGridView m_minimapPanel;
		/// @brief Page indices whose entities are currently loaded (by streaming or the minimap bake).
		std::set<uint16> m_entityPages;
		/// @brief Page indices whose entities only the minimap bake or ForceLoadPages loaded.
		std::set<uint16> m_bakeEntityPages;

		/// @brief Interactive minimap bake state (see StartMinimapBake).
		std::unique_ptr<MinimapBaker> m_minimapBake;
		std::vector<MinimapBaker::PageCoord> m_minimapBakeQueue;
		size_t m_minimapBakeNext = 0;
		MinimapBaker::Stats m_minimapBakeStats;
		uint32 m_minimapBakeFarTerrainRadius = 0;
		bool m_openMinimapBakeDialog = false;
		/// @brief Page indices whose authored foliage only ForceLoadPages loaded.
		std::set<uint16> m_bakeFoliagePages;
		/// @brief Whether the minimap panel's world name has been set yet (deferred until first draw
		///        because the asset path is known at construction but minimaps may not exist yet).
		bool m_minimapWorldSet{ false };

		/// @brief An unsaved working copy of a kit edited in this world.
		struct DirtyKit
		{
			/// @brief The working copy every placement using the kit shows.
			PropKitPtr kit;
			/// @brief The placement the kit was last edited on; not counted as "other" in the save prompt.
			uint64 originPlacementId{0};
			/// @brief Every placement the working copy was edited on since it became dirty.
			std::set<uint64> editingPlacementIds;
		};

		/// @brief Dirty kits by kit path (forward slashes, case-insensitive). Written on Save().
		std::map<String, DirtyKit, StrCaseIComp> m_dirtyKits;

		/// @brief The cached kit last applied per kit path, to notice saves from the prop kit editor.
		std::map<String, PropKitPtr, StrCaseIComp> m_observedKits;

		/// @brief Active kit per placement id, as picked in the details panel. An empty string means
		///        "None"; placements without an entry use their first kit.
		std::map<uint64, String> m_activeKits;

		/// @brief A queued kit prop removal.
		struct PendingKitPropRemoval
		{
			uint64 placementId;
			String kitPath;
			size_t propIndex;
		};

		/// @brief Kit prop removals applied after the current frame's UI.
		std::vector<PendingKitPropRemoval> m_pendingKitPropRemovals;

		/// @brief Dirty kits used by other loaded placements, waiting for the save prompt.
		std::vector<String> m_pendingKitSaves;

		/// @brief Collision display of the selected building's kit props; exists only while shown.
		std::unique_ptr<CollisionOverlay> m_kitCollisionOverlay;
		/// @brief World-space collision vertices of the displayed kit props.
		std::vector<Vector3> m_kitCollisionVertices;
		/// @brief Collision triangle list of the displayed kit props.
		std::vector<uint32> m_kitCollisionIndices;
		/// @brief Whether "Show kit collision" is ticked for m_kitCollisionPlacementId.
		bool m_showKitCollision{false};
		/// @brief Set when the displayed kits change, so the display is rebuilt.
		bool m_kitCollisionDirty{true};
		/// @brief The building the kit collision display belongs to (0: none).
		uint64 m_kitCollisionPlacementId{0};
		/// @brief Placement transform the display was built for; a moved building rebuilds it.
		Vector3 m_kitCollisionPosition;
		Quaternion m_kitCollisionOrientation;
		Vector3 m_kitCollisionScale;

		/// @brief Opens the shared-kit save prompt on the next Draw().
		bool m_openKitSavePrompt{false};

		/// @brief Opens the "Save as new kit" dialog on the next Draw().
		bool m_openSaveAsNewKit{false};

		/// @brief Placement, source kit and target path of the "Save as new kit" dialog.
		uint64 m_saveAsNewKitPlacement{0};
		String m_saveAsNewKitSource;
		String m_saveAsNewKitTarget;
	};
}

