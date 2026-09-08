// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

#include "imgui_node_editor.h"

#include <vector>

namespace mmo
{
	/// Manages the available model files in the asset registry.
	class TriggerEditorWindow final
		: public EditorEntryWindowBase<proto::Triggers, proto::TriggerEntry>
		, public NonCopyable
	{
	public:
		explicit TriggerEditorWindow(const String& name, proto::Project& project, EditorHost& host);
		~TriggerEditorWindow() override;

	private:
		void DrawDetailsImpl(proto::TriggerEntry& currentEntry) override;

		/// @brief Renders the chain around one trigger as a node graph.
		/// @details The graph is scoped to the given trigger plus, transitively, every trigger it
		///          chains to through a Trigger action and every trigger that chains to it.
		///          Rendering the whole project instead is what made this view unreadable. The
		///          graph is read-only: links are authored through the Trigger action in the edit
		///          view.
		/// @param selectedEntry The trigger the chain is built around.
		void DrawChainView(const proto::TriggerEntry& selectedEntry);

		/// @brief Collects the trigger ids reachable from a trigger in either direction.
		/// @param rootTriggerId Trigger to start from; always part of the result.
		/// @return The chain members, sorted by id so the result can be compared frame to frame.
		[[nodiscard]] std::vector<uint32> CollectChain(uint32 rootTriggerId) const;

		/// @brief Assigns node positions for a chain, left to right along the call direction.
		/// @param chain Chain members as returned by CollectChain.
		void LayoutChain(const std::vector<uint32>& chain);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		ImGuiTextFilter m_parentZoneFilter;

		/// @brief When true the details panel shows the node-graph canvas instead of the edit UI.
		bool m_showChainView = false;

		/// @brief imgui-node-editor context; created lazily on first Chain View draw.
		ax::NodeEditor::EditorContext* m_nodeEditorCtx = nullptr;

		/// @brief Non-zero when the user double-clicked a node in the chain view; causes a
		///        switch back to Edit mode and selects that trigger for editing.
		uint32 m_jumpToTriggerId = 0;

		/// @brief Chain drawn last frame, used to notice when the graph needs laying out again.
		std::vector<uint32> m_chainCache;

		/// @brief Set when node positions should be recomputed on the next chain view draw.
		///        Between relayouts nodes keep whatever position the user dragged them to.
		bool m_relayoutChain = true;
	};
}
