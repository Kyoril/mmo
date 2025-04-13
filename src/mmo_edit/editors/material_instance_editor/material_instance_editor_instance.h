// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>
#include <span>

#include "deferred_shading/deferred_renderer.h"
#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "scene_graph/scene.h"

namespace mmo
{
	class MaterialInstanceEditor;

	/// @brief An editor instance for editing a material.
	class MaterialInstanceEditorInstance final : public EditorInstance
	{
	public:
		MaterialInstanceEditorInstance(MaterialInstanceEditor& editor, EditorHost& host, const Path& assetPath);
		~MaterialInstanceEditorInstance() override;

	public:
		void Save() const;

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

	private:
		void RenderMaterialPreview();

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

		SceneNode* m_lightNode{ nullptr };
		Light* m_light{ nullptr };

		std::unique_ptr<DeferredRenderer> m_deferredRenderer;
	};
}
