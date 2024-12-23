// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>
#include <asio/io_service.hpp>

#include "selected_map_entity.h"
#include "paging/world_page_loader.h"
#include "base/id_generator.h"
#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/world_grid.h"
#include "transform_widget.h"
#include "selection.h"

namespace mmo
{
	class WorldModelEditor;
	class Entity;
	class Camera;
	class SceneNode;

	class WorldModelEditorInstance final : public EditorInstance, public ChunkReader
	{

	public:
		explicit WorldModelEditorInstance(EditorHost& host, WorldModelEditor& editor, Path asset);
		~WorldModelEditorInstance() override;

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

		void PerformEntitySelectionRaycast(float viewportX, float viewportY);

		void CreateMapEntity(const String& assetName, const Vector3& position, const Quaternion& orientation, const Vector3& scale);

		void OnMapEntityRemoved(MapEntity& entity);

	private:
		bool ReadMVERChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadMeshChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool ReadEntityChunk(io::Reader& reader, uint32 chunkHeader, uint32 chunkSize);
		bool OnReadFinished() noexcept override;

	private:
		WorldModelEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
		bool m_wireFrame;
		Scene m_scene;
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

		bool m_gridSnap { true };
		float m_translateSnapSizes[7] = { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f };
		float m_rotateSnapSizes[6] = { 1.0f, 5.0f, 10.0f, 15.0f, 45.0f, 90.0f };
		int m_currentTranslateSnapSize { 3 };
		int m_currentRotateSnapSize { 3 };

		std::unique_ptr<RaySceneQuery> m_raySceneQuery;
			
		Selection m_selection;
		std::unique_ptr<TransformWidget> m_transformWidget;

		ManualRenderObject* m_debugBoundingBox;

		float m_cameraSpeed { 20.0f };
		IdGenerator<uint64> m_objectIdGenerator { 1 };

		std::vector<std::unique_ptr<MapEntity>> m_mapEntities;
		ImVec2 m_lastContentRectMin{};

		Light* m_sunLight{ nullptr };

		std::vector<String> m_meshNames;
		Vector3 m_brushPosition{};
	};
}
