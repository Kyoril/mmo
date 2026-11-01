// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "imgui_node_layout_store.h"

#include <cfloat>

#include <imgui.h>
#include "imgui_node_editor.h"
#include "imgui_node_editor_internal.h"

namespace ed = ax::NodeEditor;

namespace mmo
{
	bool ImGuiNodeLayoutStore::GetNodeBounds(const uint32 nodeId, float& x, float& y, float& w, float& h) const
	{
		if (!ed::GetCurrentEditor())
		{
			return false;
		}

		// Read the *live* node position and size directly from the node editor rather than from the
		// serialized state snapshot (m_NodesState). That snapshot is only refreshed when an editor
		// state is applied/loaded - it is NOT updated when the user drags a node around or when a node
		// is created at runtime. GetNodePosition/GetNodeSize return the current bounds straight from
		// the editor's live node objects.
		const ImVec2 position = ed::GetNodePosition(nodeId);
		const ImVec2 size = ed::GetNodeSize(nodeId);

		// GetNodePosition returns (FLT_MAX, FLT_MAX) when the node is not known to the editor yet
		// (i.e. it has never been committed/rendered).
		if (position.x == FLT_MAX || position.y == FLT_MAX)
		{
			return false;
		}

		x = position.x;
		y = position.y;
		w = size.x;
		h = size.y;
		return true;
	}

	void ImGuiNodeLayoutStore::SetNodeBounds(const uint32 nodeId, const float x, const float y, const float w, const float h)
	{
		const auto editorContext = reinterpret_cast<ed::Detail::EditorContext*>(ed::GetCurrentEditor());
		if (!editorContext)
		{
			return;
		}

		ed::Detail::EditorState& state = editorContext->GetState();
		const auto nodeStateIt = state.m_NodesState.m_Nodes.find(nodeId);
		if (nodeStateIt == state.m_NodesState.m_Nodes.end())
		{
			state.m_NodesState.m_Nodes.emplace(nodeId, ed::Detail::NodeState{ImVec2{x, y}, ImVec2{w, h}, ImVec2{0.0f, 0.0f}});
		}
		else
		{
			nodeStateIt->second.m_Location = ImVec2{x, y};
			nodeStateIt->second.m_Size = ImVec2{w, h};
		}
	}
}
