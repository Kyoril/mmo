// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editors/editor_instance.h"

#include "pcg/graph/evaluator.h"
#include "pcg/graph/graph_document.h"

#include <imgui.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace ax::NodeEditor
{
	struct EditorContext;
}

namespace mmo
{
	class PcgGraphEditor;
	class SectionAssetCache;

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

		/// @brief Re-evaluates the document against the debug target when its inputs changed and redraws the debug
		///        layer when the evaluation or the set of debug nodes changed. Evaluates on the base terrain a rebuild
		///        sees (the target instance's own terrain contribution undone); a debug node's own terrain ops are drawn
		///        as height ticks and outlines (pcg::graph::BuildTerrainDebugGeometry). Called once per frame by the graph
		///        editor, also while this instance's window is hidden; evaluates only while a node has debug on.
		void UpdateDebugView();

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
		void InitializeDockLayout(ImGuiID dockspaceId, const String& graphId, const String& paletteId, const String& detailsId, const String& parametersId,
			const String& debugId);

		/// @brief Draws the Debug window: target graph instance, hints, errors and the legend.
		void DrawDebugPanel(const String& panelId);

		/// @brief Points the debug view at a graph instance (0, 0 = none). Clears the old layer and the cache.
		void SetDebugTarget(uint64 worldSerial, uint64 instanceId);

		/// @brief Removes this editor's debug layer from the target world (if it is still open).
		void ClearDebugLayer();

		/// @brief The asset path with forward slashes, as graph instances store it.
		[[nodiscard]] String GetNormalizedAssetPath() const;

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

		/// @brief Why the last rename of parameter m_renameErrorParameter was refused (empty = no error).
		String m_renameError;
		String m_renameErrorParameter;
		int m_newParameterType { 0 };

		/// @brief One row of the debug legend.
		struct DebugLegendEntry
		{
			uint32 node = 0;
			String title;
			uint32 argb = 0;
			/// False when there is no evaluation result for the node (the legend shows a dash).
			bool hasResult = false;
			size_t pointsDrawn = 0;
			size_t pointsTotal = 0;
			size_t linesDrawn = 0;
			size_t linesTotal = 0;
		};

		/// @brief Last owner key handed out (see m_debugOwnerKey).
		static inline uint64 s_lastDebugOwnerKey = 0;

		/// @brief Identifies this editor's debug layer in a world.
		uint64 m_debugOwnerKey { 0 };

		/// @brief Debug target: world serial (looked up every frame, never stored as a pointer) and graph instance id.
		uint64 m_debugWorldSerial { 0 };
		uint64 m_debugInstanceId { 0 };

		/// @brief Evaluation cache of the debug target; cleared when the target changes.
		pcg::graph::GraphCache m_debugCache;

		/// @brief Section assets of the debug target's terrain ops; cleared when the target changes, entries dropped
		///        when their files change (checked when m_debugSectionCheckKey differs from the evaluation key, else at
		///        most once a second, at m_debugSectionCheckTime in ImGui time).
		std::unique_ptr<SectionAssetCache> m_debugSections;
		uint64 m_debugSectionCheckKey { 0 };
		double m_debugSectionCheckTime { 0 };

		/// @brief Areas of the touching terrain-writing instances (the seams of the debug view), refreshed together with the
		///        section check (the evaluation key changed, else at most once a second): collecting reads graph files.
		std::vector<PathSpline> m_debugSeamNeighbours;

		/// @brief Last debug evaluation and the key of its inputs (0 = none yet).
		pcg::graph::EvalResult m_debugResult;
		bool m_hasDebugResult { false };
		uint64 m_debugEvalKey { 0 };
		double m_debugEvalMilliseconds { 0 };

		/// @brief Hash of the target instance's terrain contribution (pcg::graph::BaseTerrainView::HashContribution), part of the
		///        evaluation key, and the key it was computed for (contribution storage, size and terrain revision; a
		///        rebuild changes them), so it is only recomputed when the contribution may have changed.
		uint64 m_debugContributionHash { 0 };
		uint64 m_debugContributionKey { 0 };

		/// @brief Key of what the layer shows (0 = nothing drawn).
		uint64 m_debugDrawKey { 0 };

		/// @brief Whether the target world holds this editor's layer.
		bool m_debugLayerSet { false };

		/// @brief Whether the node overlay (m_nodeRunInfo) shows m_debugResult.
		bool m_debugResultsShown { false };

		/// @brief Set when building the evaluation context failed: it is retried at m_debugRetryTime (ImGui time),
		///        with a back-off that doubles up to a cap, or at once through the Retry button.
		bool m_debugContextFailed { false };
		double m_debugRetryTime { 0 };
		double m_debugRetryDelay { 0 };

		String m_debugError;
		std::vector<String> m_debugWarnings;
		std::vector<DebugLegendEntry> m_debugLegend;
	};
}
