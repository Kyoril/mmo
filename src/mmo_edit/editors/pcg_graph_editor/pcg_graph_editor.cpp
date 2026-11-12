// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pcg_graph_editor.h"

#include "pcg_graph_editor_instance.h"
#include "asset_file_name.h"
#include "editor_host.h"
#include "icon_font.h"
#include "assets/asset_registry.h"
#include "editors/world_editor/pcg/graph_instance_builder.h"
#include "log/default_log_levels.h"
#include "pcg/graph/graph_io.h"

#include <algorithm>

#include <imgui.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace mmo
{
	PcgGraphEditor::PcgGraphEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	bool PcgGraphEditor::CanLoadAsset(const String& extension) const
	{
		return extension == GraphAssetExtension;
	}

	void PcgGraphEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem(ICON_FA_SITEMAP " Create New PCG Graph"))
		{
			m_graphName.clear();
			m_showGraphNameDialog = true;
		}
	}

	void PcgGraphEditor::DrawImpl()
	{
		// Debug views follow world edits even while their graph window is hidden.
		for (const auto& entry : m_instances)
		{
			static_cast<PcgGraphEditorInstance&>(*entry.second).UpdateDebugView();
		}

		if (m_showGraphNameDialog)
		{
			ImGui::OpenPopup("Create New PCG Graph");
			m_showGraphNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New PCG Graph", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter a name for the new PCG graph:");

			ImGui::InputText("##field", &m_graphName);
			ImGui::SameLine();
			ImGui::Text("%s", GraphAssetExtension);

			// The name is one file name in the current folder; never overwrite an existing graph (graph instances
			// may already use it).
			const String nameError = AssetFileNameError(m_graphName);
			const bool exists = nameError.empty() && AssetRegistry::HasFile(GetNewGraphPath());
			if (!m_graphName.empty() && !nameError.empty())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", nameError.c_str());
			}
			if (exists)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "A file with this name already exists.");
			}

			ImGui::BeginDisabled(!nameError.empty() || exists);
			if (ImGui::Button("Create"))
			{
				CreateNewGraph();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	std::shared_ptr<EditorInstance> PcgGraphEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<PcgGraphEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open PCG graph editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void PcgGraphEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	String PcgGraphEditor::GetNewGraphPath() const
	{
		Path assetPath = m_host.GetCurrentPath();
		assetPath /= m_graphName + GraphAssetExtension;

		// The asset registry keys files by forward-slash paths.
		String path = assetPath.string();
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	}

	void PcgGraphEditor::CreateNewGraph()
	{
		const String nameError = AssetFileNameError(m_graphName);
		if (!nameError.empty())
		{
			ELOG("Invalid PCG graph name '" << m_graphName << "': " << nameError);
			return;
		}

		const String assetPath = GetNewGraphPath();
		if (AssetRegistry::HasFile(assetPath))
		{
			ELOG("PCG graph " << assetPath << " already exists; not overwriting it");
			return;
		}

		pcg::graph::Graph graph;
		graph.name = m_graphName;
		m_graphName.clear();

		const auto file = AssetRegistry::CreateNewFile(assetPath);
		if (!file)
		{
			ELOG("Failed to create new PCG graph " << assetPath);
			return;
		}

		*file << pcg::graph::WriteGraph(graph);
		file->flush();

		ILOG("Created PCG graph " << assetPath);
		m_host.assetImported(m_host.GetCurrentPath());

		OpenAsset(assetPath);
	}
}
