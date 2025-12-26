// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>
#include <assimp/scene.h>

#include "anim_evaluator.h"
#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/world_grid.h"

namespace mmo
{
	class PreviewProviderManager;
	class MeshEditor;
	class Entity;
	class Camera;
	class SceneNode;

	class MeshEditorInstance final : public EditorInstance
	{
	public:
		explicit MeshEditorInstance(EditorHost& host, MeshEditor& editor, PreviewProviderManager& previewManager, Path asset);
		~MeshEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

		bool Save() override;

	private:
		void SetAnimationState(AnimationState* animState);

		void DrawDetails(const String& id);

		void DrawAnimations(const String& id);

		void DrawBones(const String& id);

		void DrawCollision(const String& id);

		void DrawViewport(const String& id);

		void ImportAnimationFromFbx(const std::filesystem::path& path, const String& animationName);

		void ImportAdditionalSubmeshes(const std::filesystem::path& path);

		void RenderBoneNode(Bone& bone);

		void LoadDataFromNode(const aiScene* mScene, const aiNode* pNode, Mesh* mesh, const Matrix4& transform);

		bool CreateSubMesh(const String& name, int index, const aiNode* pNode, const aiMesh* aiMesh, const MaterialPtr& material, Mesh* mesh, AABB& boundingBox, const Matrix4& transform) const;

		void ComputeNodesDerivedTransform(const aiScene* mScene, const aiNode* pNode, const aiMatrix4x4& accTransform);

	private:
		MeshEditor& m_editor;
		PreviewProviderManager& m_previewManager;
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
		bool m_playAnimation = false;
		String m_newAnimationName;
		String m_animationImportPath;

		std::unique_ptr<AxisDisplay> m_selectedBoneAxis{ nullptr };
		SceneNode* m_selectedBoneNode{ nullptr };
		String m_selectedBoneName;
		Bone* m_selectedBone{ nullptr };

		std::unique_ptr<AnimEvaluator> m_animEvaluator;

		std::set<uint16> m_includedSubMeshes;

		typedef std::map<String, Matrix4> NodeTransformMap;
		NodeTransformMap mNodeDerivedTransformByName;
		String m_importSubmeshFile;

		Vector3 m_importOffset = Vector3::Zero;
		Vector3 m_importScale = Vector3::UnitScale;;
		Quaternion m_importRotation = Quaternion::Identity;
	};
}
