// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include "editor_entry_window_base.h"

#include "editor_host.h"
#include "graphics/texture.h"
#include "proto_data/project.h"

#include "imgui_node_editor.h"

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

		/// @brief Renders the trigger chain as a node graph.
		/// @details Each TriggerEntry becomes a node.  Trigger actions that chain
		///          to another trigger are shown as directed edges.
		/// @param triggers All trigger entries to render.
		void DrawChainView(const proto::Triggers& triggers);

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

		/// @brief Non-zero when the user clicked a node in the chain view; causes a
		///        switch back to Edit mode and selects that trigger for editing.
		uint32 m_jumpToTriggerId = 0;
	};
}
