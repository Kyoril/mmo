// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>

#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/world_grid.h"

namespace mmo
{
	class ModelEditor;
	class Entity;
	class Camera;
	class SceneNode;

	class ModelEditorInstance final : public EditorInstance
	{
	public:
		explicit ModelEditorInstance(EditorHost& host, ModelEditor& editor, Path asset);
		~ModelEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

	private:
		void Save();

		void SetAnimationState(AnimationState* animState);

		void DrawDetails(const String& id);

		void DrawAnimations(const String& id);

		void DrawBones(const String& id);

		void DrawViewport(const String& id);

		void ImportAnimationFromFbx(const std::filesystem::path& path, const String& animationName);

		void RenderBoneNode(const Bone& bone);

	private:
		ModelEditor& m_editor;
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
		bool m_middleButtonPressed { false };
		bool m_initDockLayout { true };
		MeshPtr m_mesh;
		MeshEntry m_entry { };
		AnimationState* m_animState{ nullptr };
		String m_newAnimationName;
		String m_animationImportPath;

		std::unique_ptr<AxisDisplay> m_selectedBoneAxis{ nullptr };
		SceneNode* m_selectedBoneNode{ nullptr };
		String m_selectedBoneName;
	};
}
