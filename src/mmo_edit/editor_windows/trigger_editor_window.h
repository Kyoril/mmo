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
	/// What a blueprint node stands for. The graph only ever contains a trigger's own events and
	/// actions, because those are the only things the data model gives an identity to.
	enum class BlueprintNodeKind
	{
		/// No node - an empty selection, or an id from a graph that is no longer on screen.
		None,

		/// A TriggerEvent, addressed by its index in the trigger's event list.
		Event,

		/// A TriggerAction, addressed by its index in the trigger's action list.
		Action,
	};

	/// A blueprint node, as kind plus index into the corresponding repeated field. Indices shift
	/// when an entry is added or removed, so one of these is only good for the frame it came from
	/// and is re-validated against the current sizes before use.
	struct BlueprintNodeRef
	{
		BlueprintNodeKind kind = BlueprintNodeKind::None;
		int index = -1;
	};

	/// What the blueprint's layout depends on. The graph's shape is fully determined by the data,
	/// so comparing this is enough to know when the nodes need placing again.
	struct BlueprintShape
	{
		uint32 triggerId = 0;
		int eventCount = -1;
		int actionCount = -1;

		bool operator==(const BlueprintShape& other) const
		{
			return triggerId == other.triggerId
				&& eventCount == other.eventCount
				&& actionCount == other.actionCount;
		}
	};

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

		/// @brief Renders one trigger as a blueprint-style graph with a docked details panel.
		/// @details Red event nodes feed the blue action chain, mirroring how the world server runs
		///          a trigger: any event starts it, the probability roll and condition gate it, and
		///          the actions then run in list order from index 0. That topology is entirely
		///          determined by the data, so the links are computed and link editing is refused;
		///          structure is changed by adding, deleting and reordering nodes instead.
		/// @param currentEntry The trigger to render and edit.
		void DrawBlueprintView(proto::TriggerEntry& currentEntry);

		/// @brief Emits the blueprint's nodes and links. Called between Begin and End.
		/// @param currentEntry The trigger being rendered.
		void DrawBlueprintCanvas(proto::TriggerEntry& currentEntry);

		/// @brief Places the blueprint's nodes: events in a left column, actions in a row.
		/// @param currentEntry The trigger being rendered.
		void LayoutBlueprint(const proto::TriggerEntry& currentEntry);

		/// @brief Node and background context menus, which are how nodes are created and reordered.
		/// @param currentEntry The trigger being edited.
		void DrawBlueprintContextMenus(proto::TriggerEntry& currentEntry);

		/// @brief The docked panel that edits the selected node, or the trigger itself when the
		///        selection is empty.
		/// @param currentEntry The trigger being edited.
		void DrawBlueprintDetails(proto::TriggerEntry& currentEntry);

	public:
		bool IsDockable() const override { return true; }

		[[nodiscard]] DockDirection GetDefaultDockDirection() const override { return DockDirection::Center; }

	private:
		EditorHost& m_host;
		ImGuiTextFilter m_parentZoneFilter;

		/// @brief When true the trigger is shown as a blueprint graph rather than as forms. The
		///        blueprint is the default; the form view remains for bulk list work.
		bool m_showBlueprintView = true;

		/// @brief Width of the blueprint's docked details panel, in pixels.
		static constexpr float s_blueprintDetailsWidth = 480.0f;

		/// @brief Content width of an event node, in pixels. Fixed so nodes line up in a column.
		static constexpr float s_blueprintEventWidth = 230.0f;

		/// @brief Content width of an action node, in pixels.
		static constexpr float s_blueprintActionWidth = 230.0f;

		/// @brief imgui-node-editor context; created lazily on first Chain View draw.
		ax::NodeEditor::EditorContext* m_nodeEditorCtx = nullptr;

		/// @brief Non-zero when the user double-clicked a Trigger action node; selects that
		///        trigger for editing on the next frame.
		uint32 m_jumpToTriggerId = 0;

		/// @brief Shape the blueprint was last laid out for; a change means the nodes move again.
		BlueprintShape m_blueprintShape;

		/// @brief Set when node positions should be recomputed on the next blueprint draw. Between
		///        relayouts nodes keep whatever position the user dragged them to.
		bool m_relayoutBlueprint = true;

		/// @brief The node whose properties the details panel is showing.
		BlueprintNodeRef m_blueprintSelection;

		/// @brief Node to select once it exists. A node added through the context menu is not known
		///        to the node editor until the following frame, so selecting it has to wait.
		BlueprintNodeRef m_blueprintPendingSelect;

		/// @brief The node a context menu was opened on, captured because the menu outlives the
		///        query that reported it.
		BlueprintNodeRef m_blueprintContextNode;
	};
}
