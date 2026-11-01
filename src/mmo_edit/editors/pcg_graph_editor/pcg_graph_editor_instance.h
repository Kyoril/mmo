// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_instance.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph_document.h"

#include <imgui.h>

#include <map>
#include <set>
#include <vector>

namespace ax::NodeEditor
{
	struct EditorContext;
}

namespace mmo
{
	class PcgGraphEditor;

	/// @brief Editor instance for one PCG graph asset (.hpcgg): a dockspace with the graph canvas, a node
	///        palette, the selected node's details and the graph parameters. Every edit goes through a
	///        pcg::graph::GraphDocument; node positions live in the document (editorX / editorY).
	class PcgGraphEditorInstance final : public EditorInstance
	{
	public:
		/// @brief Opens a graph for editing.
		/// @param host The editor host.
		/// @param editor The owning graph editor (its modified / saved signals are raised).
		/// @param asset The asset path of the graph.
		explicit PcgGraphEditorInstance(EditorHost& host, PcgGraphEditor& editor, Path asset);

		/// @brief Destroys the node editor context.
		~PcgGraphEditorInstance() override;

	public:
		/// @copydoc EditorInstance::Draw
		void Draw() override;

		/// @brief Writes the graph to its file. Refuses to save a graph whose file could not be parsed.
		/// @copydoc EditorInstance::Save
		bool Save() override;

		/// @brief Gets the edited graph document.
		[[nodiscard]] const pcg::graph::GraphDocument& GetDocument() const { return m_document; }

		/// @brief Whether the document has changes that are not saved.
		[[nodiscard]] bool HasUnsavedChanges() const { return m_document.Revision() != m_savedRevision; }

		/// @brief Shows the results of an evaluation of the document on the nodes (time, cache marker, warnings,
		///        errors). Results of nodes no longer in the graph are ignored.
		void SetNodeResults(const pcg::graph::EvalResult& result);

	private:
		/// @brief What the canvas shows about a node's last evaluation.
		struct NodeRunInfo
		{
			double milliseconds = 0;
			bool fromCache = false;
			std::vector<String> warnings;
			String error;
		};

	private:
		void LoadFromFile();

		void DrawGraphPanel(const String& panelId);
		void DrawPalettePanel(const String& panelId);
		void DrawDetailsPanel(const String& panelId);
		void DrawParametersPanel(const String& panelId);
		void InitializeDockLayout(ImGuiID dockspaceId, const String& graphId, const String& paletteId, const String& detailsId, const String& parametersId);

		/// @brief Submits every node and link to the node editor.
		void CommitNodes();
		void CommitNode(const pcg::graph::NodeInstance& node);
		void DrawNodeTooltip();
		void HandleCreateAction();
		void HandleDeleteAction();
		void HandleContextMenus();

		/// @brief Draws the create-node popup (palette at the mouse) while the node editor is suspended.
		void DrawCreateNodePopup();

		/// @brief Draws a search field and the node types matching it, grouped by category.
		/// @param filter The search text.
		/// @param focusFilter Whether to put the keyboard focus into the search field.
		/// @param listSize Size of the scrolling list (0 = fill the available space).
		/// @return The node type the user picked this frame, or an empty string.
		String DrawPaletteList(String& filter, bool focusFilter, const ImVec2& listSize);

		/// @brief Adds a node at a canvas position and, if a pin is given, links it to the first fitting pin.
		void CreateNode(const String& type, const ImVec2& canvasPosition, uint64 linkFromPin);

		/// @brief Copies node positions changed in the node editor into the document.
		void SyncPositionsFromEditor();

		/// @brief Re-validates the graph when the document changed.
		void RefreshIssues();

		/// @brief Raises the editor's modified signal when the document became dirty.
		void UpdateDirtyState();

		void DrawSettings(const pcg::graph::NodeInstance& node, const pcg::graph::NodeTypeDef& def);

		/// @brief Draws the "bind to parameter" combo of one setting.
		void DrawBindingCombo(const pcg::graph::NodeInstance& node, const pcg::graph::SettingDef& setting, const pcg::graph::AttributeValue& value);

	private:
		PcgGraphEditor& m_editor;
		ax::NodeEditor::EditorContext* m_context { nullptr };
		pcg::graph::GraphDocument m_document;

		/// @brief Set when the file could not be read or parsed; editing and saving are disabled then.
		String m_loadError;
		uint64 m_savedRevision { 0 };
		bool m_wasDirty { false };

		bool m_initDockLayout { true };
		int m_framesUntilNavigate { 2 };

		/// @brief Nodes whose document position was handed to the node editor.
		std::set<uint32> m_placedNodes;

		/// @brief Pins with at least one link, rebuilt every frame by CommitNodes.
		std::set<uint64> m_linkedPins;
		uint32 m_selectedNode { 0 };

		/// @brief Validation result, refreshed when the document revision changes.
		uint64 m_issuesRevision { ~uint64(0) };
		std::map<uint32, std::vector<String>> m_nodeErrors;
		std::vector<String> m_graphIssues;

		std::map<uint32, NodeRunInfo> m_nodeRunInfo;

		/// @brief Canvas position of the graph panel's centre (where palette clicks add nodes).
		ImVec2 m_viewCenter { 0.0f, 0.0f };

		String m_paletteFilter;
		String m_popupFilter;
		bool m_openCreatePopup { false };
		bool m_focusPopupFilter { false };
		ImVec2 m_createPosition { 0.0f, 0.0f };
		uint64 m_createFromPin { 0 };

		String m_newParameterName;
		int m_newParameterType { 0 };
	};
}
