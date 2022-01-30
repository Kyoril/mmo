// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <imgui.h>
#include <span>

#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "scene_graph/material.h"
#include "scene_graph/scene.h"

namespace mmo
{
	struct NodeTypeInfo;
	class MaterialGraph;
	class Node;
	class Pin;

	// Dialog for picking new node type
	struct CreateNodeDialog
	{
	    void Open(Pin* fromPin = nullptr);
	    void Show(MaterialGraph& document);

	    Node* GetCreatedNode()       { return m_CreatedNode; }
	    const Node* GetCreatedNode() const { return m_CreatedNode; }

	    std::span<Pin*>       GetCreatedLinks()       { return m_CreatedLinks; }
		std::span<const Pin* const> GetCreatedLinks() const { return {const_cast<const Pin* const*>(m_CreatedLinks.data()), m_CreatedLinks.size() }; }

	private:
	    std::vector<Pin*> CreateLinkToFirstMatchingPin(Node& node, Pin& fromPin);

	    Node* m_CreatedNode = nullptr;
	    std::vector<Pin*> m_CreatedLinks;

	    std::vector<const NodeTypeInfo*> m_SortedNodes;
	};

	/// @brief An editor instance for editing a material.
	class MaterialEditorInstance final : public EditorInstance
	{
	public:
		MaterialEditorInstance(EditorHost& host, const Path& assetPath);
		~MaterialEditorInstance() override;

	public:
		void Compile();
		void Save();

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;

		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;

		void OnMouseMoved(uint16 x, uint16 y) override;

	private:
		void HandleCreateAction(MaterialGraph& material);

		void RenderMaterialPreview();

		void HandleDeleteAction(MaterialGraph& material);

	private:
		scoped_connection m_renderConnection;
		ImVec2 m_lastAvailViewportSize;
		RenderTexturePtr m_viewportRT;
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
		float m_columnWidth { 400.0f };
		CreateNodeDialog m_createDialog;
		std::shared_ptr<Material> m_material;
	};
}
