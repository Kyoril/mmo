// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pcg_graph_editor_instance.h"

#include "pcg_graph_editor.h"
#include "setting_widgets.h"

#include "assets/asset_registry.h"
#include "editor_windows/editor_imgui_helpers.h"
#include "icon_font.h"
#include "editors/material_editor/node_editor/item_builder.h"
#include "editors/material_editor/node_editor/item_deleter.h"
#include "editors/material_editor/node_editor/node_pin_icons.h"
#include "log/default_log_levels.h"
#include "pcg/graph/hashing.h"
#include "pcg/graph/node_registry.h"

#include <imgui_internal.h>
#include <imgui_node_editor.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>

namespace ed = ax::NodeEditor;

namespace mmo
{
	namespace
	{
		using pcg::graph::AttributeType;
		using pcg::graph::AttributeValue;
		using pcg::graph::GraphDocument;
		using pcg::graph::ItemKind;
		using pcg::graph::NodeInstance;
		using pcg::graph::NodeTypeDef;
		using pcg::graph::ParameterDef;
		using pcg::graph::PinDef;
		using pcg::graph::SettingDef;

		const char* const s_attributeTypeNames[] = { "Float", "Int", "Bool", "Vector3", "String" };

		const ImVec4 s_errorColor(1.0f, 0.35f, 0.35f, 1.0f);
		const ImVec4 s_warningColor(1.0f, 0.85f, 0.3f, 1.0f);

		/// Header colour of a node category (unknown categories get a stable colour from their name).
		ImColor CategoryColor(const String& category)
		{
			static const std::map<String, ImColor> s_colors = {
				{ "Input", ImColor(0.25f, 0.55f, 0.30f) },
				{ "Sampler", ImColor(0.20f, 0.40f, 0.70f) },
				{ "Filter", ImColor(0.70f, 0.45f, 0.15f) },
				{ "Point", ImColor(0.50f, 0.30f, 0.65f) },
				{ "Spatial", ImColor(0.15f, 0.55f, 0.55f) },
				{ "Spawners", ImColor(0.65f, 0.55f, 0.15f) }
			};
			const auto it = s_colors.find(category);
			if (it != s_colors.end())
			{
				return it->second;
			}
			const float hue = static_cast<float>(pcg::graph::StringHash(category) % 360) / 360.0f;
			return ImColor::HSV(hue, 0.5f, 0.55f);
		}

		/// Colour of a pin by the item kinds it carries: one kind has its own colour, any or several kinds are grey.
		ImVec4 KindColor(const PinDef* pin)
		{
			if (pin == nullptr || pin->accepts.size() != 1)
			{
				return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
			}
			switch (pin->accepts.front())
			{
			case ItemKind::Points: return ImVec4(0.40f, 0.70f, 1.00f, 1.0f);
			case ItemKind::Spline: return ImVec4(0.45f, 0.90f, 0.45f, 1.0f);
			case ItemKind::Area: return ImVec4(1.00f, 0.65f, 0.30f, 1.0f);
			case ItemKind::Params: return ImVec4(0.80f, 0.50f, 1.00f, 1.0f);
			}
			return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
		}

		String KindNames(const PinDef* pin)
		{
			if (pin == nullptr || pin->accepts.empty())
			{
				return "any";
			}
			String names;
			for (const ItemKind kind : pin->accepts)
			{
				const char* name = kind == ItemKind::Points ? "Points" : kind == ItemKind::Spline ? "Spline" : kind == ItemKind::Area ? "Area" : "Params";
				names += (names.empty() ? "" : ", ") + String(name);
			}
			return names;
		}

		String ValueToText(const AttributeValue& value)
		{
			char buffer[96];
			switch (pcg::graph::TypeOf(value))
			{
			case AttributeType::Float:
				std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(std::get<float>(value)));
				return buffer;
			case AttributeType::Int:
				return std::to_string(std::get<int64>(value));
			case AttributeType::Bool:
				return std::get<bool>(value) ? "true" : "false";
			case AttributeType::Vector3:
			{
				const Vector3& v = std::get<Vector3>(value);
				std::snprintf(buffer, sizeof(buffer), "%g, %g, %g", static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z));
				return buffer;
			}
			case AttributeType::String:
				return "\"" + std::get<String>(value) + "\"";
			}
			return {};
		}

		String ToLower(String text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c)
			{
				return static_cast<char>(std::tolower(c));
			});
			return text;
		}

		bool HasParameter(const pcg::graph::Graph& graph, const String& name)
		{
			return std::any_of(graph.parameters.begin(), graph.parameters.end(), [&name](const ParameterDef& p)
			{
				return p.name == name;
			});
		}

		/// `base`, or `base2`, `base3`, ... whichever the graph does not use as a parameter name yet.
		String UniqueParameterName(const pcg::graph::Graph& graph, const String& base)
		{
			String name = base.empty() ? String("param") : base;
			for (int suffix = 2; HasParameter(graph, name); ++suffix)
			{
				name = base + std::to_string(suffix);
			}
			return name;
		}

		/// Why a name cannot be used for a new or renamed parameter, or empty if it can. Names are identifiers
		/// ([A-Za-z_][A-Za-z0-9_]*), so a binding "$name" is never ambiguous (no leading '$').
		String ParameterNameError(const pcg::graph::Graph& graph, const String& name)
		{
			if (name.empty())
			{
				return "The name is empty.";
			}
			const auto isIdentifierChar = [](const char c)
			{
				return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
			};
			if ((name[0] >= '0' && name[0] <= '9') || !std::all_of(name.begin(), name.end(), isIdentifierChar))
			{
				return "Use letters, digits and '_' only, not starting with a digit.";
			}
			if (HasParameter(graph, name))
			{
				return "A parameter with this name exists.";
			}
			return {};
		}
	}

	PcgGraphEditorInstance::PcgGraphEditorInstance(EditorHost& host, PcgGraphEditor& editor, Path asset)
		: EditorInstance(host, std::move(asset))
		, m_editor(editor)
	{
		ed::Config config;
		config.SettingsFile = nullptr;
		m_context = ed::CreateEditor(&config);

		LoadFromFile();
	}

	PcgGraphEditorInstance::~PcgGraphEditorInstance()
	{
		if (m_context != nullptr)
		{
			if (ed::GetCurrentEditor() == m_context)
			{
				ed::SetCurrentEditor(nullptr);
			}
			ed::DestroyEditor(m_context);
			m_context = nullptr;
		}
	}

	void PcgGraphEditorInstance::LoadFromFile()
	{
		String path = GetAssetPath().string();
		std::replace(path.begin(), path.end(), '\\', '/');

		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(path);
		if (!file)
		{
			m_loadError = "Unable to open " + path;
			ELOG("PCG graph editor: " << m_loadError);
			return;
		}

		const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
		String error;
		if (!m_document.Load(text, pcg::graph::NodeRegistry::Default(), error))
		{
			m_loadError = path + " is not a valid PCG graph: " + error;
			ELOG("PCG graph editor: " << m_loadError);
			return;
		}

		m_savedRevision = m_document.Revision();
		m_placedNodes.clear();
	}

	void PcgGraphEditorInstance::Draw()
	{
		ImGui::PushID(GetAssetPath().string().c_str());

		const ImGuiID dockspaceId = ImGui::GetID("##pcg_graph_dockspace");
		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

		ed::SetCurrentEditor(m_context);

		const String graphId = "Graph##" + GetAssetPath().string();
		const String paletteId = "Palette##" + GetAssetPath().string();
		const String detailsId = "Details##" + GetAssetPath().string();
		const String parametersId = "Parameters##" + GetAssetPath().string();

		RefreshIssues();

		DrawGraphPanel(graphId);
		DrawPalettePanel(paletteId);
		DrawDetailsPanel(detailsId);
		DrawParametersPanel(parametersId);

		InitializeDockLayout(dockspaceId, graphId, paletteId, detailsId, parametersId);

		UpdateDirtyState();

		ed::SetCurrentEditor(nullptr);
		ImGui::PopID();
	}

	bool PcgGraphEditorInstance::Save()
	{
		String path = GetAssetPath().string();
		std::replace(path.begin(), path.end(), '\\', '/');

		if (!m_loadError.empty())
		{
			// Never overwrite a file this editor could not read: it would lose its content.
			ELOG("Not saving PCG graph " << path << ": it could not be loaded (" << m_loadError << ")");
			return false;
		}

		if (m_context != nullptr)
		{
			ed::EditorContext* previous = ed::GetCurrentEditor();
			ed::SetCurrentEditor(m_context);
			SyncPositionsFromEditor();
			ed::SetCurrentEditor(previous);
		}

		const std::unique_ptr<std::ostream> stream = AssetRegistry::CreateNewFile(path);
		if (!stream)
		{
			ELOG("Failed to save PCG graph " << path << ": unable to open file for writing");
			return false;
		}

		*stream << m_document.Save();
		stream->flush();

		m_savedRevision = m_document.Revision();
		m_wasDirty = false;
		m_editor.saved();
		ILOG("Saved PCG graph " << path << " (" << m_document.GetGraph().nodes.size() << " nodes, " << m_document.GetGraph().links.size() << " links)");
		return true;
	}

	void PcgGraphEditorInstance::SetNodeResults(const pcg::graph::EvalResult& result)
	{
		m_nodeRunInfo.clear();
		for (const auto& entry : result.nodes)
		{
			if (m_document.FindNode(entry.first) == nullptr)
			{
				continue;
			}

			NodeRunInfo info;
			info.milliseconds = entry.second.milliseconds;
			info.fromCache = entry.second.fromCache;
			info.warnings = entry.second.output.warnings;
			info.error = entry.second.output.error;
			m_nodeRunInfo[entry.first] = std::move(info);
		}
	}

	void PcgGraphEditorInstance::DrawGraphPanel(const String& panelId)
	{
		if (ImGui::Begin(panelId.c_str()))
		{
			ImGui::BeginDisabled(!m_loadError.empty());
			if (ImGui::Button(ICON_FA_SAVE " Save"))
			{
				Save();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (HasUnsavedChanges())
			{
				ImGui::TextColored(s_warningColor, "Unsaved changes");
			}
			else
			{
				ImGui::TextDisabled("Saved");
			}

			size_t nodeIssues = 0;
			for (const auto& entry : m_nodeErrors)
			{
				nodeIssues += entry.second.size();
			}
			ImGui::SameLine();
			if (nodeIssues + m_graphIssues.size() == 0)
			{
				ImGui::TextDisabled("| No issues");
			}
			else
			{
				ImGui::TextColored(s_errorColor, "| %zu issue(s)", nodeIssues + m_graphIssues.size());
			}
			for (const String& issue : m_graphIssues)
			{
				ImGui::TextColored(s_errorColor, "%s", issue.c_str());
			}

			if (!m_loadError.empty())
			{
				ImGui::TextColored(s_errorColor, "%s", m_loadError.c_str());
				ImGui::TextWrapped("The graph cannot be edited or saved. Fix or replace the file, then reopen it.");
			}
			else
			{
				const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
				const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

				ed::Begin("##pcg_graph_canvas", ImVec2(0.0f, 0.0f));
				m_viewCenter = ed::ScreenToCanvas(ImVec2(canvasMin.x + canvasSize.x * 0.5f, canvasMin.y + canvasSize.y * 0.5f));

				CommitNodes();
				HandleCreateAction();
				HandleDeleteAction();
				HandleContextMenus();

				ed::NodeId selected;
				m_selectedNode = ed::GetSelectedNodes(&selected, 1) > 0 ? static_cast<uint32>(selected.Get()) : 0;

				// Positions are copied into the document once a drag ends, not on every frame of it.
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					SyncPositionsFromEditor();
				}

				// Node sizes are known after the first frame; frame the content then.
				if (m_framesUntilNavigate > 0 && --m_framesUntilNavigate == 0)
				{
					ed::NavigateToContent(0.0f);
				}

				DrawNodeTooltip();

				ed::Suspend();
				DrawCreateNodePopup();
				ed::Resume();

				ed::End();
			}
		}
		ImGui::End();
	}

	void PcgGraphEditorInstance::CommitNodes()
	{
		const std::vector<GraphDocument::ResolvedLink> links = m_document.ResolveLinks();
		m_linkedPins.clear();
		for (const GraphDocument::ResolvedLink& link : links)
		{
			m_linkedPins.insert(link.fromPin);
			m_linkedPins.insert(link.toPin);
		}

		for (const NodeInstance& node : m_document.GetGraph().nodes)
		{
			CommitNode(node);
		}

		for (const GraphDocument::ResolvedLink& link : links)
		{
			ed::Link(link.id, link.fromPin, link.toPin, KindColor(m_document.FindPinDef(link.fromPin)), 2.0f);
		}
	}

	void PcgGraphEditorInstance::CommitNode(const NodeInstance& node)
	{
		const NodeTypeDef* def = m_document.FindNodeType(node.id);
		const auto errors = m_nodeErrors.find(node.id);
		const auto run = m_nodeRunInfo.find(node.id);
		const bool hasError = def == nullptr || errors != m_nodeErrors.end() || (run != m_nodeRunInfo.end() && !run->second.error.empty());
		const bool hasWarning = run != m_nodeRunInfo.end() && !run->second.warnings.empty();

		if (m_placedNodes.insert(node.id).second)
		{
			ed::SetNodePosition(node.id, ImVec2(node.editorX, node.editorY));
		}

		if (hasError)
		{
			ed::PushStyleColor(ed::StyleColor_NodeBorder, s_errorColor);
			ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f);
		}

		ed::BeginNode(node.id);
		ImGui::PushID(static_cast<int>(node.id));

		// Header: title (and the warning mark), drawn over a bar in the category colour.
		const String title = def != nullptr ? def->title : "Unknown: " + node.type;
		ImGui::TextUnformatted(title.c_str());
		if (hasWarning)
		{
			ImGui::SameLine();
			ImGui::TextColored(s_warningColor, "(!)");
		}
		const float headerBottom = ImGui::GetItemRectMax().y + ImGui::GetStyle().ItemSpacing.y * 0.5f;
		ImGui::Dummy(ImVec2(150.0f, 1.0f));

		// Last evaluation: time and cache marker.
		if (run != m_nodeRunInfo.end())
		{
			ImGui::TextDisabled("%.2f ms", run->second.milliseconds);
			ImGui::SameLine();
			if (run->second.fromCache)
			{
				ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "cached");
			}
			else
			{
				ImGui::TextDisabled("ran");
			}
		}
		else
		{
			ImGui::TextDisabled("-- ms");
		}

		const float iconSize = ImGui::GetTextLineHeight();
		const std::vector<String> inputs = m_document.GetPinNames(node.id, false);
		const std::vector<String> outputs = m_document.GetPinNames(node.id, true);

		ImGui::BeginGroup();
		for (size_t i = 0; i < inputs.size(); ++i)
		{
			const uint64 pinId = GraphDocument::MakePinId(node.id, false, static_cast<uint32>(i));
			ed::BeginPin(pinId, ed::PinKind::Input);
			ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
			Icon(ImVec2(iconSize, iconSize), IconType::Circle, m_linkedPins.count(pinId) != 0, KindColor(m_document.FindPinDef(pinId)));
			ImGui::SameLine();
			ImGui::TextUnformatted(inputs[i].c_str());
			ed::EndPin();
		}
		ImGui::EndGroup();

		ImGui::SameLine(0.0f, 24.0f);

		float outputWidth = 0.0f;
		for (const String& name : outputs)
		{
			outputWidth = std::max(outputWidth, ImGui::CalcTextSize(name.c_str()).x);
		}

		ImGui::BeginGroup();
		for (size_t i = 0; i < outputs.size(); ++i)
		{
			const uint64 pinId = GraphDocument::MakePinId(node.id, true, static_cast<uint32>(i));
			ed::BeginPin(pinId, ed::PinKind::Output);
			ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
			const float indent = outputWidth - ImGui::CalcTextSize(outputs[i].c_str()).x;
			if (indent > 0.0f)
			{
				ImGui::Dummy(ImVec2(indent, 0.0f));
				ImGui::SameLine(0.0f, 0.0f);
			}
			ImGui::TextUnformatted(outputs[i].c_str());
			ImGui::SameLine();
			Icon(ImVec2(iconSize, iconSize), IconType::Circle, m_linkedPins.count(pinId) != 0, KindColor(m_document.FindPinDef(pinId)));
			ed::EndPin();
		}
		ImGui::EndGroup();

		ImGui::PopID();
		ed::EndNode();

		if (hasError)
		{
			ed::PopStyleVar();
			ed::PopStyleColor();
		}

		if (ImGui::IsItemVisible())
		{
			const ImVec2 position = ed::GetNodePosition(node.id);
			const ImVec2 size = ed::GetNodeSize(node.id);
			const float halfBorder = ed::GetStyle().NodeBorderWidth * 0.5f;
			ImColor color = def != nullptr ? CategoryColor(def->category) : ImColor(0.75f, 0.15f, 0.15f);
			color.Value.w = 0.85f;
			ed::GetNodeBackgroundDrawList(node.id)->AddRectFilled(
				ImVec2(position.x + halfBorder, position.y + halfBorder),
				ImVec2(position.x + size.x - halfBorder, headerBottom),
				color, ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
		}
	}

	void PcgGraphEditorInstance::DrawNodeTooltip()
	{
		const uint32 id = static_cast<uint32>(ed::GetHoveredNode().Get());
		const NodeInstance* node = id != 0 ? m_document.FindNode(id) : nullptr;
		if (node == nullptr)
		{
			return;
		}

		std::vector<std::pair<ImVec4, String>> lines;
		if (m_document.IsUnknownNode(id))
		{
			lines.emplace_back(s_errorColor, "Unknown node type '" + node->type + "': kept as is, cannot be evaluated.");
		}
		const auto errors = m_nodeErrors.find(id);
		if (errors != m_nodeErrors.end())
		{
			for (const String& error : errors->second)
			{
				lines.emplace_back(s_errorColor, error);
			}
		}
		const auto run = m_nodeRunInfo.find(id);
		if (run != m_nodeRunInfo.end())
		{
			if (!run->second.error.empty())
			{
				lines.emplace_back(s_errorColor, run->second.error);
			}
			for (const String& warning : run->second.warnings)
			{
				lines.emplace_back(s_warningColor, warning);
			}
		}
		if (!node->comment.empty())
		{
			lines.emplace_back(ImGui::GetStyleColorVec4(ImGuiCol_Text), node->comment);
		}
		if (lines.empty())
		{
			return;
		}

		ed::Suspend();
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
		for (const auto& line : lines)
		{
			ImGui::TextColored(line.first, "%s", line.second.c_str());
		}
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
		ed::Resume();
	}

	void PcgGraphEditorInstance::HandleCreateAction()
	{
		ItemBuilder builder;
		if (!builder)
		{
			return;
		}

		if (const LinkBuilder* linkBuilder = builder.QueryNewLink())
		{
			// The user may drag either way; links always go from an output to an input.
			uint64 fromPin = linkBuilder->startPinId.Get();
			uint64 toPin = linkBuilder->endPinId.Get();
			if (!GraphDocument::DecodePinId(fromPin).isOutput && GraphDocument::DecodePinId(toPin).isOutput)
			{
				std::swap(fromPin, toPin);
			}

			String reason;
			if (m_document.CanLink(fromPin, toPin, reason))
			{
				if (linkBuilder->Accept())
				{
					m_document.AddLink(fromPin, toPin);
				}
			}
			else
			{
				ed::Suspend();
				ImGui::SetTooltip("Cannot link: %s", reason.c_str());
				ed::Resume();
				linkBuilder->Reject();
			}
		}
		else if (const NodeBuilder* nodeBuilder = builder.QueryNewNode())
		{
			ed::Suspend();
			ImGui::SetTooltip("Create node...");
			ed::Resume();

			if (nodeBuilder->Accept())
			{
				// Inside the canvas the mouse position is in canvas space.
				m_createPosition = ImGui::GetMousePos();
				m_createFromPin = nodeBuilder->pinId.Get();
				m_openCreatePopup = true;
			}
		}
	}

	void PcgGraphEditorInstance::HandleDeleteAction()
	{
		ItemDeleter deleter;
		if (!deleter)
		{
			return;
		}

		std::vector<uint32> nodes;
		while (ItemDeleter::NodeDeleter* nodeDeleter = deleter.QueryDeletedNode())
		{
			if (nodeDeleter->Accept(true))
			{
				nodes.push_back(static_cast<uint32>(nodeDeleter->nodeId.Get()));
			}
		}

		while (ItemDeleter::LinkDeleter* linkDeleter = deleter.QueryDeleteLink())
		{
			if (linkDeleter->Accept())
			{
				m_document.RemoveLink(linkDeleter->linkId.Get());
			}
		}

		for (const uint32 id : nodes)
		{
			m_document.RemoveNode(id);
			m_placedNodes.erase(id);
			if (m_selectedNode == id)
			{
				m_selectedNode = 0;
			}
		}
	}

	void PcgGraphEditorInstance::HandleContextMenus()
	{
		if (ed::ShowBackgroundContextMenu())
		{
			m_createPosition = ImGui::GetMousePos();
			m_createFromPin = 0;
			m_openCreatePopup = true;
		}
	}

	void PcgGraphEditorInstance::DrawCreateNodePopup()
	{
		const char* popupId = "##pcg_create_node";
		if (m_openCreatePopup)
		{
			ImGui::OpenPopup(popupId);
			m_openCreatePopup = false;
			m_popupFilter.clear();
			m_focusPopupFilter = true;
		}

		if (ImGui::BeginPopup(popupId))
		{
			const String type = DrawPaletteList(m_popupFilter, m_focusPopupFilter, ImVec2(280.0f, 320.0f));
			m_focusPopupFilter = false;
			if (!type.empty())
			{
				CreateNode(type, m_createPosition, m_createFromPin);
				m_createFromPin = 0;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		else
		{
			m_createFromPin = 0;
		}
	}

	String PcgGraphEditorInstance::DrawPaletteList(String& filter, const bool focusFilter, const ImVec2& listSize)
	{
		if (focusFilter)
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(listSize.x > 0.0f ? listSize.x : -FLT_MIN);
		ImGui::InputTextWithHint("##filter", ICON_FA_SEARCH " Search nodes", &filter);

		const String needle = ToLower(filter);
		std::vector<const NodeTypeDef*> matches;
		for (const NodeTypeDef* def : m_document.GetRegistry().All())
		{
			if (needle.empty() || ToLower(def->title).find(needle) != String::npos || ToLower(def->type).find(needle) != String::npos
				|| ToLower(def->category).find(needle) != String::npos)
			{
				matches.push_back(def);
			}
		}

		String picked;
		if (ImGui::BeginChild("##palette_list", listSize))
		{
			if (matches.empty())
			{
				ImGui::TextDisabled("No node type matches.");
			}

			// All() is sorted by category, so every category is one run of entries.
			size_t i = 0;
			while (i < matches.size())
			{
				const String& category = matches[i]->category;
				size_t end = i;
				while (end < matches.size() && matches[end]->category == category)
				{
					++end;
				}

				// While searching, every category is open.
				ImGui::SetNextItemOpen(true, needle.empty() ? ImGuiCond_Once : ImGuiCond_Always);
				const String header = (category.empty() ? String("Other") : category) + "##category";
				if (ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
				{
					for (size_t k = i; k < end; ++k)
					{
						const NodeTypeDef* def = matches[k];
						ImGui::PushID(def->type.c_str());
						if (ImGui::Selectable(def->title.c_str()))
						{
							picked = def->type;
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("%s\n%zu input(s), %zu output(s), %zu setting(s)", def->type.c_str(), def->inputs.size(), def->outputs.size(), def->settings.size());
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
				i = end;
			}
		}
		ImGui::EndChild();
		return picked;
	}

	void PcgGraphEditorInstance::CreateNode(const String& type, const ImVec2& canvasPosition, const uint64 linkFromPin)
	{
		const uint32 id = m_document.AddNode(type, std::trunc(canvasPosition.x), std::trunc(canvasPosition.y));
		if (id == 0 || linkFromPin == 0)
		{
			return;
		}

		// Link the dragged pin to the first fitting pin of the new node.
		const bool fromOutput = GraphDocument::DecodePinId(linkFromPin).isOutput;
		const std::vector<String> pins = m_document.GetPinNames(id, !fromOutput);
		for (size_t i = 0; i < pins.size(); ++i)
		{
			const uint64 pin = GraphDocument::MakePinId(id, !fromOutput, static_cast<uint32>(i));
			const uint64 fromPin = fromOutput ? linkFromPin : pin;
			const uint64 toPin = fromOutput ? pin : linkFromPin;
			String reason;
			if (m_document.CanLink(fromPin, toPin, reason))
			{
				m_document.AddLink(fromPin, toPin);
				break;
			}
		}
	}

	void PcgGraphEditorInstance::SyncPositionsFromEditor()
	{
		// The node editor truncates positions toward zero (IM_FLOOR is (float)(int)x), so a fractional position
		// from the file is not a change.
		std::vector<std::pair<uint32, ImVec2>> moved;
		for (const NodeInstance& node : m_document.GetGraph().nodes)
		{
			if (m_placedNodes.count(node.id) == 0)
			{
				continue;
			}
			const ImVec2 position = ed::GetNodePosition(node.id);
			if (position.x == FLT_MAX || position.y == FLT_MAX)
			{
				continue;
			}
			if (position.x != std::trunc(node.editorX) || position.y != std::trunc(node.editorY))
			{
				moved.emplace_back(node.id, position);
			}
		}

		for (const auto& entry : moved)
		{
			m_document.SetNodePosition(entry.first, entry.second.x, entry.second.y);
		}
	}

	void PcgGraphEditorInstance::RefreshIssues()
	{
		if (m_issuesRevision == m_document.Revision())
		{
			return;
		}
		m_issuesRevision = m_document.Revision();

		m_nodeErrors.clear();
		m_graphIssues.clear();
		if (!m_loadError.empty())
		{
			return;
		}
		for (const pcg::graph::GraphIssue& issue : pcg::graph::ValidateGraph(m_document.GetGraph(), m_document.GetRegistry()))
		{
			if (issue.node != 0 && m_document.FindNode(issue.node) != nullptr)
			{
				m_nodeErrors[issue.node].push_back(issue.message);
			}
			else
			{
				m_graphIssues.push_back(issue.message);
			}
		}
	}

	void PcgGraphEditorInstance::UpdateDirtyState()
	{
		const bool dirty = HasUnsavedChanges();
		if (dirty && !m_wasDirty)
		{
			m_editor.modified();
		}
		m_wasDirty = dirty;
	}

	void PcgGraphEditorInstance::DrawPalettePanel(const String& panelId)
	{
		if (ImGui::Begin(panelId.c_str()))
		{
			if (m_loadError.empty())
			{
				const String type = DrawPaletteList(m_paletteFilter, false, ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing() * 2.5f));
				if (!type.empty())
				{
					CreateNode(type, m_viewCenter, 0);
				}
				ImGui::TextWrapped("Click adds at the view centre. Right-click the canvas or drop a link on empty space to add at the mouse.");
			}
		}
		ImGui::End();
	}

	void PcgGraphEditorInstance::DrawDetailsPanel(const String& panelId)
	{
		if (ImGui::Begin(panelId.c_str()))
		{
			const NodeInstance* node = m_loadError.empty() ? m_document.FindNode(m_selectedNode) : nullptr;
			if (node == nullptr)
			{
				ImGui::TextDisabled("Select a node in the graph to edit its settings.");
				ImGui::End();
				return;
			}

			const uint32 id = node->id;

			// Scoped by node id: a text field still active when the selection changes must not commit its text
			// into the newly selected node (same widget ids for nodes of the same type).
			ImGui::PushID(static_cast<int>(id));

			const NodeTypeDef* def = m_document.FindNodeType(id);
			ImGui::TextUnformatted(def != nullptr ? def->title.c_str() : node->type.c_str());
			ImGui::TextDisabled("%s, id %u", node->type.c_str(), id);

			const auto errors = m_nodeErrors.find(id);
			if (errors != m_nodeErrors.end())
			{
				for (const String& error : errors->second)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, s_errorColor);
					ImGui::TextWrapped("%s", error.c_str());
					ImGui::PopStyleColor();
				}
			}
			const auto run = m_nodeRunInfo.find(id);
			if (run != m_nodeRunInfo.end())
			{
				if (!run->second.error.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, s_errorColor);
					ImGui::TextWrapped("%s", run->second.error.c_str());
					ImGui::PopStyleColor();
				}
				for (const String& warning : run->second.warnings)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, s_warningColor);
					ImGui::TextWrapped("%s", warning.c_str());
					ImGui::PopStyleColor();
				}
			}

			String comment = node->comment;
			if (ImGui::InputTextMultiline("Comment", &comment, ImVec2(0.0f, ImGui::GetTextLineHeight() * 3.0f)))
			{
				m_document.SetNodeComment(id, comment);
			}

			ImGui::Separator();

			if (def == nullptr)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, s_errorColor);
				ImGui::TextWrapped("Unknown node type: its settings, links and pins are kept as they are.");
				ImGui::PopStyleColor();
				for (const auto& setting : node->settings)
				{
					ImGui::BulletText("%s = %s", setting.first.c_str(), ValueToText(setting.second).c_str());
				}
			}
			else
			{
				DrawSettings(*node, *def);

				// Settings the type does not declare (from another version of the node) are kept but unused.
				bool first = true;
				for (const auto& setting : node->settings)
				{
					const bool declared = std::any_of(def->settings.begin(), def->settings.end(), [&setting](const SettingDef& s)
					{
						return s.name == setting.first;
					});
					if (declared)
					{
						continue;
					}
					if (first)
					{
						ImGui::Spacing();
						ImGui::TextDisabled("Undeclared settings (kept, not used):");
						first = false;
					}
					ImGui::BulletText("%s = %s", setting.first.c_str(), ValueToText(setting.second).c_str());
				}
			}

			ImGui::PopID();
		}
		ImGui::End();
	}

	void PcgGraphEditorInstance::DrawSettings(const NodeInstance& node, const NodeTypeDef& def)
	{
		if (def.settings.empty())
		{
			ImGui::TextDisabled("This node has no settings.");
			return;
		}

		const uint32 id = node.id;
		for (const SettingDef& setting : def.settings)
		{
			ImGui::PushID(setting.name.c_str());

			const auto found = node.settings.find(setting.name);
			const AttributeValue value = found != node.settings.end() ? found->second : setting.defaultValue;
			const AttributeType type = pcg::graph::TypeOf(setting.defaultValue);

			String parameter;
			if (pcg::graph::IsParameterBinding(value, parameter))
			{
				ImGui::LabelText(setting.name.c_str(), "bound to '%s'", parameter.c_str());
			}
			else
			{
				// Literal Strings starting with "$" are stored escaped ("$$..."); the widget shows the literal.
				AttributeValue shown = pcg::graph::UnescapeSettingValue(value);
				if (pcg::graph::TypeOf(shown) != type)
				{
					shown = setting.defaultValue;
				}
				if (DrawAttributeValueWidget(setting.name.c_str(), shown, type, setting.enumValues, setting.min, setting.max, setting.widget))
				{
					if (type == AttributeType::String)
					{
						const String& text = std::get<String>(shown);
						if (!text.empty() && text[0] == '$')
						{
							shown = String("$" + text);
						}
					}
					m_document.SetSetting(id, setting.name, shown);
				}
			}

			if (!setting.description.empty())
			{
				ImGui::SameLine();
				DrawHelpMarker(setting.description.c_str());
			}

			ImGui::Indent();
			DrawBindingCombo(node, setting, value);
			ImGui::Unindent();

			ImGui::PopID();
		}
	}

	void PcgGraphEditorInstance::DrawBindingCombo(const NodeInstance& node, const SettingDef& setting, const AttributeValue& value)
	{
		const uint32 id = node.id;
		const AttributeType type = pcg::graph::TypeOf(setting.defaultValue);

		String bound;
		const bool isBound = pcg::graph::IsParameterBinding(value, bound);

		// Parameters of the setting's type (copied: picking one edits the document).
		std::vector<ParameterDef> candidates;
		for (const ParameterDef& parameter : m_document.GetGraph().parameters)
		{
			if (pcg::graph::TypeOf(parameter.defaultValue) == type)
			{
				candidates.push_back(parameter);
			}
		}

		const String preview = isBound ? "$" + bound : String("(not bound)");
		ImGui::SetNextItemWidth(std::max(120.0f, ImGui::CalcItemWidth() - ImGui::GetStyle().IndentSpacing));
		if (ImGui::BeginCombo("bind to parameter", preview.c_str()))
		{
			if (ImGui::Selectable("(not bound)", !isBound) && isBound)
			{
				// Unbinding keeps the parameter's default as the literal value when it fits.
				AttributeValue literal = setting.defaultValue;
				for (const ParameterDef& parameter : candidates)
				{
					if (parameter.name == bound)
					{
						literal = parameter.defaultValue;
					}
				}
				m_document.SetSetting(id, setting.name, literal);
			}

			for (const ParameterDef& parameter : candidates)
			{
				if (ImGui::Selectable(parameter.name.c_str(), isBound && parameter.name == bound))
				{
					m_document.SetSetting(id, setting.name, AttributeValue(String("$" + parameter.name)));
				}
			}

			ImGui::Separator();
			if (ImGui::Selectable(ICON_FA_PLUS " New parameter from this setting"))
			{
				// The new parameter starts with the setting's current literal value.
				AttributeValue initial = isBound ? setting.defaultValue : pcg::graph::UnescapeSettingValue(value);
				if (pcg::graph::TypeOf(initial) != type)
				{
					initial = setting.defaultValue;
				}

				ParameterDef parameter;
				parameter.name = UniqueParameterName(m_document.GetGraph(), setting.name);
				parameter.defaultValue = initial;
				parameter.description = setting.description;
				m_document.EditGraph().parameters.push_back(parameter);
				m_document.SetSetting(id, setting.name, AttributeValue(String("$" + parameter.name)));
			}
			ImGui::EndCombo();
		}
	}

	void PcgGraphEditorInstance::DrawParametersPanel(const String& panelId)
	{
		if (ImGui::Begin(panelId.c_str()))
		{
			if (!m_loadError.empty())
			{
				ImGui::End();
				return;
			}

			// Copied: every edit goes through EditGraph.
			const std::vector<ParameterDef> parameters = m_document.GetGraph().parameters;
			if (parameters.empty())
			{
				ImGui::TextDisabled("The graph has no parameters.");
			}

			int removeIndex = -1;
			for (size_t i = 0; i < parameters.size(); ++i)
			{
				const ParameterDef& parameter = parameters[i];
				// Scoped by name, not index: removing a row must not hand an active field to the next parameter.
				ImGui::PushID(parameter.name.c_str());

				String name = parameter.name;
				ImGui::SetNextItemWidth(150.0f);
				if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue) && name != parameter.name)
				{
					const String error = ParameterNameError(m_document.GetGraph(), name);
					m_renameErrorParameter = error.empty() ? String() : parameter.name;
					m_renameError = error;
					if (error.empty())
					{
						// Rename the parameter and every binding to it.
						pcg::graph::Graph& graph = m_document.EditGraph();
						graph.parameters[i].name = name;
						const String oldBinding = "$" + parameter.name;
						for (NodeInstance& node : graph.nodes)
						{
							for (auto& setting : node.settings)
							{
								const String* text = std::get_if<String>(&setting.second);
								if (text != nullptr && *text == oldBinding)
								{
									setting.second = String("$" + name);
								}
							}
						}
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Press Enter to rename; bindings follow the new name.");
				}
				const bool showRenameError = !m_renameError.empty() && m_renameErrorParameter == parameter.name;

				ImGui::SameLine();
				int typeIndex = static_cast<int>(pcg::graph::TypeOf(parameter.defaultValue));
				ImGui::SetNextItemWidth(90.0f);
				if (ImGui::Combo("##type", &typeIndex, s_attributeTypeNames, IM_ARRAYSIZE(s_attributeTypeNames)))
				{
					m_document.EditGraph().parameters[i].defaultValue = pcg::graph::DefaultValue(static_cast<AttributeType>(typeIndex));
				}

				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FA_TRASH))
				{
					removeIndex = static_cast<int>(i);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Remove the parameter (settings bound to it then report an issue).");
				}
				if (showRenameError)
				{
					ImGui::TextColored(s_errorColor, "Not renamed: %s", m_renameError.c_str());
				}

				AttributeValue defaultValue = parameter.defaultValue;
				if (DrawAttributeValueWidget("Default", defaultValue, pcg::graph::TypeOf(parameter.defaultValue), {}, 0.0f, 0.0f, String()))
				{
					m_document.EditGraph().parameters[i].defaultValue = defaultValue;
				}

				String description = parameter.description;
				if (ImGui::InputText("Description", &description))
				{
					m_document.EditGraph().parameters[i].description = description;
				}

				ImGui::Separator();
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				std::vector<ParameterDef>& edited = m_document.EditGraph().parameters;
				edited.erase(edited.begin() + removeIndex);
			}

			ImGui::Spacing();
			ImGui::SetNextItemWidth(150.0f);
			ImGui::InputTextWithHint("##new_name", "New parameter", &m_newParameterName);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			ImGui::Combo("##new_type", &m_newParameterType, s_attributeTypeNames, IM_ARRAYSIZE(s_attributeTypeNames));
			ImGui::SameLine();
			const String nameError = ParameterNameError(m_document.GetGraph(), m_newParameterName);
			ImGui::BeginDisabled(!nameError.empty());
			if (ImGui::Button(ICON_FA_PLUS " Add"))
			{
				ParameterDef parameter;
				parameter.name = m_newParameterName;
				parameter.defaultValue = pcg::graph::DefaultValue(static_cast<AttributeType>(m_newParameterType));
				m_document.EditGraph().parameters.push_back(parameter);
				m_newParameterName.clear();
			}
			ImGui::EndDisabled();
			if (!m_newParameterName.empty() && !nameError.empty())
			{
				ImGui::TextColored(s_errorColor, "%s", nameError.c_str());
			}
		}
		ImGui::End();
	}

	void PcgGraphEditorInstance::InitializeDockLayout(const ImGuiID dockspaceId, const String& graphId, const String& paletteId, const String& detailsId, const String& parametersId)
	{
		if (!m_initDockLayout)
		{
			return;
		}

		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_AutoHideTabBar);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

		const float width = ImGui::GetMainViewport()->Size.x;
		ImGuiID mainId = dockspaceId;
		ImGuiID rightId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Right, 380.0f / width, nullptr, &mainId);
		const ImGuiID leftId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 260.0f / std::max(width - 380.0f, 1.0f), nullptr, &mainId);
		const ImGuiID rightBottomId = ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Down, 0.4f, nullptr, &rightId);

		ImGui::DockBuilderDockWindow(graphId.c_str(), mainId);
		ImGui::DockBuilderDockWindow(paletteId.c_str(), leftId);
		ImGui::DockBuilderDockWindow(detailsId.c_str(), rightId);
		ImGui::DockBuilderDockWindow(parametersId.c_str(), rightBottomId);
		ImGui::DockBuilderFinish(dockspaceId);
		m_initDockLayout = false;
	}
}
