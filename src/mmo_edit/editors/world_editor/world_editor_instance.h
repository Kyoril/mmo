// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
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
#include "transform_widget.h"
#include "selection.h"
#include "nav_mesh/map.h"
#include "terrain/terrain.h"
#include "edit_modes/terrain_edit_mode.h"
#include "edit_modes/spawn_edit_mode.h"
#include "edit_modes/entity_edit_mode.h"
#include "scene_graph/octree_scene.h"
#include "graphics/sky_component.h"

#include "deferred_shading/deferred_renderer.h"
#include "edit_modes/navigation_edit_mode.h"
#include "edit_modes/sky_edit_mode.h"
#include "scene_outline_window.h"

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

	/// @brief Represents a single entity on the map.
	class MapEntity final : public NonCopyable
	{
	public:
		typedef signal<void(const MapEntity&)> TransformChangedSignal;
		TransformChangedSignal transformChanged;

		typedef signal<void(MapEntity&)> RemoveSignal;
		RemoveSignal remove;

	public:
		explicit MapEntity(Scene& scene, SceneNode& sceneNode, Entity& entity, uint32 uniqueId)
			: m_scene(scene)
			, m_sceneNode(sceneNode)
			, m_entity(entity)
			, m_uniqueId(uniqueId)
		{
		}

		~MapEntity() override
		{
			m_scene.DestroyEntity(m_entity);
			m_scene.DestroySceneNode(m_sceneNode);
		}

	public:
		SceneNode& GetSceneNode() const
		{
			return m_sceneNode;
		}

		Entity& GetEntity() const
		{
			return m_entity;
		}

		uint64 GetUniqueId() const
		{
			return m_uniqueId;
		}

		void ClearReferencePosition()
		{
			m_referencePagePosition.reset();
		}

		void SetReferencePagePosition(const PagePosition& position)
		{
			m_referencePagePosition = position;
		}

		const std::optional<PagePosition>& GetReferencePagePosition() const
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

		const String& GetDisplayName() const
		{
			return m_name;
		}

		const String& GetCategory() const
		{
			return m_category;
		}

		void SetDisplayName(const String& name)
		{
			m_name = name;
			m_modified = true;
		}

		void SetCategory(const String& category)
		{
			m_category = category;
			m_modified = true;
		}

	private:
		Scene& m_scene;
		SceneNode& m_sceneNode;
		Entity& m_entity;
		uint64 m_uniqueId;
		std::optional<PagePosition> m_referencePagePosition;
		bool m_modified{ false };
		String m_name;
		String m_category;
	};

	struct WorldPage
	{

	};

	class SelectableVisitor
	{
	public:
		virtual ~SelectableVisitor() = default;

		virtual void Visit(SelectedMapEntity& selectable) = 0;

		virtual void Visit(SelectedTerrainTile& selectable) = 0;

		virtual void Visit(SelectedUnitSpawn& selectable) = 0;

		virtual void Visit(SelectedObjectSpawn& selectable) = 0;
	};

	class WorldEditorInstance final : public EditorInstance, public IPageLoaderListener, public SelectableVisitor, public ChunkReader, public IWorldEditor
	{
	public:
		explicit WorldEditorInstance(EditorHost& host, WorldEditor& editor, Path asset);
		~WorldEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

		bool Save() override;
	private:
		// UI related methods
		void HandleKeyboardShortcuts();
		void DrawDetailsPanel(const String& detailsId);
		void DrawEditModeSelector();
		void DrawSelectionDetails();
		void DrawWorldSettingsPanel(const String& worldSettingsId);
		void DrawViewportPanel(const String& viewportId);
		void DrawSceneOutlinePanel(const String& sceneOutlineId);
		void HandleViewportInteractions(const ImVec2& availableSpace);
		void DrawViewportToolbar(const ImVec2& availableSpace);
		void DrawSnapSettings();
		void DrawTransformButtons(const ImVec2& availableSpace);
		void HandleViewportDragDrop();
		void InitializeDockLayout(ImGuiID dockspaceId, const String& viewportId, const String& detailsId, const String& worldSettingsId);
		
		void UpdateDebugAABB(const AABB& aabb);

		void PerformEntitySelectionRaycast(float viewportX, float viewportY);

		void PerformSpawnSelectionRaycast(float viewportX, float viewportY);

		void PerformTerrainSelectionRaycast(float viewportX, float viewportY);

		void OnTerrainMouseMoved(float viewportX, float viewportY);

		Entity* CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale, uint64 objectId) override;

		Entity* CreateUnitSpawnEntity(proto::UnitSpawnEntry& spawn);

		Entity* CreateObjectSpawnEntity(proto::ObjectSpawnEntry& spawn);

		void OnMapEntityRemoved(MapEntity& entity);

		PagePosition GetPagePositionFromCamera() const;

		void SetMapEntry(proto::MapEntry* entry);

	public:
		void OnPageAvailabilityChanged(const PageNeighborhood& page, bool isAvailable) override;

		void EnsurePageIsLoaded(PagePosition pos);

		void Visit(SelectedMapEntity& selectable) override;

		void Visit(SelectedTerrainTile& selectable) override;

		void Visit(SelectedUnitSpawn& selectable) override;

		void Visit(SelectedObjectSpawn& selectable) override;

	private:
		bool ReadMVERChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadEntityChunkV2(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool ReadTerrainChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);

		bool OnReadFinished() override;

		void SetEditMode(WorldEditMode* editMode);

	public:
		void ClearSelection() override;

		void RemoveAllUnitSpawns() override;

		void AddUnitSpawn(proto::UnitSpawnEntry& spawn, bool select) override;

		void AddObjectSpawn(proto::ObjectSpawnEntry& spawn) override;

		Camera& GetCamera() const override;

		bool IsGridSnapEnabled() const override { return m_gridSnap; }

		float GetTranslateGridSnapSize() const override { return m_translateSnapSizes[m_currentTranslateSnapSize]; }

		bool HasTerrain() const override { return m_hasTerrain; }

		terrain::Terrain* GetTerrain() const override { return m_terrain.get(); }

	private:
		uint16 BuildPageIndex(uint8 x, uint8 y) const;

		bool GetPageCoordinatesFromIndex(uint16 pageIndex, uint8& x, uint8& y) const;

		void LoadPageEntities(uint8 x, uint8 y);

		void UnloadPageEntities(uint8 x, uint8 y);

	public:
		const std::filesystem::path GetWorldPath() const override { return m_assetPath; }

	private:
		WorldEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		bool m_wireFrame;
		OctreeScene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Entity* m_entity { nullptr };
		Camera* m_camera { nullptr };
		std::unique_ptr<WorldGrid> m_worldGrid;
		int16 m_lastMouseX { 0 }, m_lastMouseY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		bool m_initDockLayout { true };
		MeshPtr m_mesh;
		MeshEntry m_entry { };
		Vector3 m_cameraVelocity{};
		bool m_hovering{ false };

		std::unique_ptr<terrain::Terrain> m_terrain;

		bool m_gridSnap { true };
		float m_translateSnapSizes[7] = { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f };
		float m_rotateSnapSizes[6] = { 1.0f, 5.0f, 10.0f, 15.0f, 45.0f, 90.0f };
		int m_currentTranslateSnapSize { 3 };
		int m_currentRotateSnapSize { 3 };
		
		std::unique_ptr<asio::io_service::work> m_work;
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::thread m_backgroundLoader;
		std::unique_ptr<LoadedPageSection> m_visibleSection;
		std::unique_ptr<WorldPageLoader> m_pageLoader;
		std::unique_ptr<PagePOVPartitioner> m_memoryPointOfView;
		std::unique_ptr<RaySceneQuery> m_raySceneQuery;

		std::vector<SceneNode*> m_spawnNodes;
		std::vector<Entity*> m_spawnEntities;
			
		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;

		ManualRenderObject* m_debugBoundingBox;

		float m_cameraSpeed { 20.0f };
		IdGenerator<uint64> m_objectIdGenerator { 1 };
		IdGenerator<uint64> m_unitSpawnIdGenerator{ 1 };
		IdGenerator<uint64> m_objectSpawnIdGenerator{ 1 };

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
		std::unique_ptr<SkyComponent> m_skyComponent;
		WorldEditMode* m_editMode{ nullptr };

		// Spawn edit mode
		proto::MapEntry* m_mapEntry { nullptr };

		Entity* m_debugEntity{ nullptr };
		SceneNode* m_debugNode{ nullptr };

		bool m_hasTerrain{ true };
		std::vector<String> m_meshNames;

		uint32 m_worldFileVersion;

		std::unique_ptr<DeferredRenderer> m_deferredRenderer;
		static TexturePtr s_translateIcon;
		static TexturePtr s_rotateIcon;
		static TexturePtr s_scaleIcon;

		std::unique_ptr<SceneOutlineWindow> m_sceneOutlineWindow;
	};
}
