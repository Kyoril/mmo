// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <span>

#include "editors/editor_instance.h"
#include "scene_graph/material.h"

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

	    Node*           m_CreatedNode = nullptr;
	    std::vector<Pin*>    m_CreatedLinks;

	    std::vector<const NodeTypeInfo*> m_SortedNodes;
	};

	/// @brief An editor instance for editing a material.
	class MaterialEditorInstance final : public EditorInstance
	{
	public:
		MaterialEditorInstance(EditorHost& host, const Path& assetPath);

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;

	private:
		void HandleCreateAction(MaterialGraph& material);

	private:
		float m_previewSize { 400.0f };
		float m_detailsSize { 100.0f };
		CreateNodeDialog m_createDialog;
		std::shared_ptr<Material> m_material;
	};
}
