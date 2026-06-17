// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>
#include <span>
#include <set>

#include "deferred_shading/deferred_renderer.h"
#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "scene_graph/scene.h"

namespace mmo
{
	class MaterialInstanceEditor;
	class PreviewProviderManager;

	/// @brief An editor instance for editing a material instance.
	class MaterialInstanceEditorInstance final : public EditorInstance
	{
	public:
		/// @brief Constructs a new MaterialInstanceEditorInstance.
		/// @param editor The parent editor.
		/// @param host The editor host.
		/// @param assetPath The path to the material instance asset.
		MaterialInstanceEditorInstance(MaterialInstanceEditor& editor, EditorHost& host, const Path& assetPath);

		/// @brief Destroys the MaterialInstanceEditorInstance.
		~MaterialInstanceEditorInstance() override;

	public:
		/// @brief Saves the material instance to disk.
		/// @return True if the save was successful, false otherwise.
		bool Save() override;

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;
		
		/// @copydoc EditorInstance::OnMouseButtonDown
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::OnMouseButtonUp
		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		/// @copydoc EditorInstance::OnMouseMoved
		void OnMouseMoved(uint16 x, uint16 y) override;

	private:
		/// @brief Renders the material preview.
		void RenderMaterialPreview();

		/// @brief Draws the details panel.
		/// @param id The ImGui ID for the panel.
		void DrawDetailsPanel(const String& id);

		/// @brief Draws the parent material section.
		void DrawParentMaterialSection();

		/// @brief Draws the scalar parameters section.
		void DrawScalarParametersSection();

		/// @brief Draws the vector parameters section.
		void DrawVectorParametersSection();

		/// @brief Draws the texture parameters section.
		void DrawTextureParametersSection();

		/// @brief Draws the material properties section.
		void DrawMaterialPropertiesSection();

		/// @brief Draws the terrain foliage override section.
		void DrawFoliageSection();

		/// @brief Changes the parent material and refreshes parameters.
		/// @param newParentPath The path to the new parent material.
		void ChangeParentMaterial(const String& newParentPath);

	private:
		MaterialInstanceEditor& m_editor;
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		Scene m_scene;
		SceneNode* m_cameraAnchor { nullptr };
		SceneNode* m_cameraNode { nullptr };
		Entity* m_entity { nullptr };
		Camera* m_camera { nullptr };
		int16 m_lastMouseX { 0 }, m_lastMouseY { 0 };
		bool m_leftButtonPressed { false };
		bool m_rightButtonPressed { false };
		float m_previewSize { 0.0f };
		float m_detailsSize { 100.0f };
		std::shared_ptr<MaterialInstance> m_material;
		bool m_initDockLayout { true };
		ImGuiTextFilter m_assetFilter;

		SceneNode* m_lightNode { nullptr };
		Light* m_light { nullptr };

		std::unique_ptr<DeferredRenderer> m_deferredRenderer;

		/// @brief The allowed extensions for parent material selection.
		static const std::set<String> s_parentMaterialExtensions;
		
		/// @brief The allowed extensions for texture parameter selection.
		static const std::set<String> s_textureExtensions;
	};
}
