// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <imgui_node_editor.h>
#include "node_builder.h"
#include "link_builder.h"

namespace mmo
{
	/// @brief Helper class for building ImGui Node Editor items like links or new nodes.
	class ItemBuilder final
	{
	public:
	    ItemBuilder()
			: m_wasCreated(ax::NodeEditor::BeginCreate(ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight)))
	    {
	    }

	    ~ItemBuilder()
	    {
			ax::NodeEditor::EndCreate();
	    }

	    explicit operator bool() const { return m_wasCreated; }

	    NodeBuilder* QueryNewNode()
	    {
		    if (m_wasCreated && ax::NodeEditor::QueryNewNode(&m_nodeBuilder.pinId))
		    {
		    	return &m_nodeBuilder;
			}

			return nullptr;
	    }

	    LinkBuilder* QueryNewLink()
	    {
		    if (m_wasCreated && ax::NodeEditor::QueryNewLink(&m_linkBuilder.startPinId, &m_linkBuilder.endPinId))
			{
		    	return &m_linkBuilder;
			}

			return nullptr;
	    }

	private:
	    bool m_wasCreated = false;
	    NodeBuilder m_nodeBuilder;
	    LinkBuilder m_linkBuilder;
	};
}
