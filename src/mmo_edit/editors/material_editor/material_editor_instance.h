// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui.h>
#include <span>

#include "editors/editor_instance.h"
#include "graphics/render_texture.h"
#include "graphics/material.h"
#include "scene_graph/scene.h"

namespace ax
{
	namespace NodeEditor
	{
		struct EditorContext;
	}
}

namespace mmo
{
	class PropertyBase;
	class MaterialEditor;
	struct NodeTypeInfo;
	class MaterialGraph;
	class GraphNode;
	class Pin;

	// Dialog for picking new node type
	struct CreateNodeDialog
	{
	    void Open(Pin* fromPin = nullptr);
	    void Show(MaterialGraph& material);

		GraphNode* GetCreatedNode()       { return m_CreatedNode; }
	    const GraphNode* GetCreatedNode() const { return m_CreatedNode; }

	    std::span<Pin*>       GetCreatedLinks()       { return m_CreatedLinks; }
		std::span<const Pin* const> GetCreatedLinks() const { return {const_cast<const Pin* const*>(m_CreatedLinks.data()), m_CreatedLinks.size() }; }

	private:
	    static std::vector<Pin*> CreateLinkToFirstMatchingPin(GraphNode& node, Pin& fromPin);

		GraphNode* m_CreatedNode = nullptr;
	    std::vector<Pin*> m_CreatedLinks;

	    std::vector<const NodeTypeInfo*> m_SortedNodes;
		
		ImGuiTextFilter m_filter;
	};

	/// @brief An editor instance for editing a material.
	class MaterialEditorInstance final : public EditorInstance
	{
	public:
		MaterialEditorInstance(MaterialEditor& editor, EditorHost& host, const Path& assetPath);
		~MaterialEditorInstance() override;

	public:
		void Compile() const;
		bool Save() override;

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;
		
		void OnMouseButtonDown(uint32 button, uint16 x, uint16 y) override;
		void OnMouseButtonUp(uint32 button, uint16 x, uint16 y) override;
		void OnMouseMoved(uint16 x, uint16 y) override;

	private:
		// Panel drawing methods
		void DrawPreviewPanel(const String& panelId);
		void DrawDetailsPanel(const String& panelId);
		void DrawGraphPanel(const String& panelId);

		// UI component methods
		void DrawPreviewToolbar();
		void DrawPreviewViewport();
		void DrawPropertyTable();
		void DrawNodeProperties(GraphNode* node);
		void DrawPropertyEditor(PropertyBase* prop);
		
		// Layout management
		void InitializeDockLayout(ImGuiID dockspaceId, const String& previewId, const String& detailsId, const String& graphId);

		// Action handling methods
		void HandleCreateAction(MaterialGraph& material);
		void RenderMaterialPreview();
		static void HandleDeleteAction(MaterialGraph& material);
		void HandleContextMenuAction(MaterialGraph& material);

	private:
		MaterialEditor& m_editor;
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
		std::unique_ptr<MaterialGraph> m_graph;
		ax::NodeEditor::EditorContext* m_context { nullptr };
		bool m_initDockLayout { true };
		ImGuiTextFilter m_assetFilter;
	};
}
