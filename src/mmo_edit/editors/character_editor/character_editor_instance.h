// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"

#include <imgui.h>

#include "editors/editor_instance.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "graphics/render_texture.h"
#include "scene_graph/scene.h"
#include "scene_graph/axis_display.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/world_grid.h"

namespace mmo
{
	class CharacterEditor;
	class Entity;
	class Camera;
	class SceneNode;

	class CharacterEditorInstance final : public EditorInstance, public CustomizationPropertyGroupApplier
	{
	public:
		explicit CharacterEditorInstance(EditorHost& host, CharacterEditor& editor, Path asset);
		~CharacterEditorInstance() override;

	public:
		/// Renders the actual 3d viewport content.
		void Render();

		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

	private:
		void Save();

		void DrawDetails(const String& id);

		void DrawViewport(const String& id);

		void DrawPropertyGroupDetails(CustomizationPropertyGroup& propertyGroup);

		void DrawVisibleSubEntityList(std::vector<std::string>& visibleSubEntities);

		void DrawPropertyGroupDetails(VisibilitySetPropertyGroup& visProp);

		void DrawPropertyGroupDetails(MaterialOverridePropertyGroup& matProp);

		void DrawMaterialMap(std::unordered_map<std::string, std::string>& subEntityToMaterial);

		void DrawPreview(const String& id);

		void UpdateBaseMesh();

		void UpdatePreview();

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;

		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;

		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;

	private:
		CharacterEditor& m_editor;
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

		std::set<uint16> m_includedSubMeshes;

		ImGuiTextFilter m_assetFilter;
		std::shared_ptr<CustomizableAvatarDefinition> m_avatarDefinition;
		AvatarConfiguration m_configuration;
		std::map<String, String> m_propertyValues;
	};
}
