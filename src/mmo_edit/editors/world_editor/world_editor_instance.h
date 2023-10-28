// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>
#include <thread>
#include <asio/io_service.hpp>

#include "world_page_loader.h"
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

namespace mmo
{
	class WorldEditor;
	class Entity;
	class Camera;
	class SceneNode;

	class WorldEditorInstance final : public EditorInstance, public IPageLoaderListener
	{
		/// @brief Represents a single entity on the map.
		struct MapEntity
		{
			SceneNode* sceneNode { nullptr };
			Entity* entity { nullptr };
		};

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

	private:
		void Save();

		void UpdateDebugAABB(const AABB& aabb);

	public:
		void OnPageAvailabilityChanged(const PageNeighborhood& page, bool isAvailable) override;

	private:
		WorldEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		bool m_wireFrame;
		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Entity* m_entity { nullptr };
		Camera* m_camera { nullptr };
		std::unique_ptr<AxisDisplay> m_axisDisplay;
		std::unique_ptr<WorldGrid> m_worldGrid;
		int16 m_lastMouseX { 0 }, m_lastMouseY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		bool m_initDockLayout { true };
		MeshPtr m_mesh;
		MeshEntry m_entry { };
		Vector3 m_cameraVelocity{};
		
		std::unique_ptr<asio::io_service::work> m_work;
		asio::io_service m_workQueue;
		asio::io_service m_dispatcher;
		std::thread m_backgroundLoader;
		std::unique_ptr<LoadedPageSection> m_visibleSection;
		std::unique_ptr<WorldPageLoader> m_pageLoader;
		std::unique_ptr<PagePOVPartitioner> m_memoryPointOfView;
		std::unique_ptr<RaySceneQuery> m_raySceneQuery;
			
		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;

		ManualRenderObject* m_debugBoundingBox;

		float m_cameraSpeed { 20.0f };
		IdGenerator<uint64> m_objectIdGenerator { 1 };

		std::vector<MapEntity> m_mapEntities;
		MapEntity* m_selectedMapEntity{ nullptr };
		ImVec2 m_lastContentRectMin{};
	};
}
