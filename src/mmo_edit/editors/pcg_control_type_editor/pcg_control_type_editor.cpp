// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pcg_control_type_editor.h"

#include "pcg_control_type_editor_instance.h"
#include "editor_host.h"
#include "icon_font.h"
#include "assets/asset_registry.h"
#include "editors/pcg_graph_editor/asset_file_name.h"
#include "editors/world_editor/edit_modes/procedural_edit_mode.h"
#include "editors/world_editor/world_editor_instance.h"
#include "log/default_log_levels.h"
#include "pcg/graph/controls.h"

#include <algorithm>

#include <imgui.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace mmo
{
	PcgControlTypeEditor::PcgControlTypeEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	bool PcgControlTypeEditor::CanLoadAsset(const String& extension) const
	{
		return extension == AssetExtension;
	}

	void PcgControlTypeEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem(ICON_FA_SLIDERS " Create New Control Type"))
		{
			m_typeName.clear();
			m_showNameDialog = true;
		}
	}

	void PcgControlTypeEditor::DrawImpl()
	{
		if (m_showNameDialog)
		{
			ImGui::OpenPopup("Create New Control Type");
			m_showNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Control Type", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter a name for the new control type:");

			ImGui::InputText("##field", &m_typeName);
			ImGui::SameLine();
			ImGui::Text("%s", AssetExtension);

			// The name is one file name in the current folder; never overwrite an existing type (placed
			// controls may already use it).
			const String nameError = AssetFileNameError(m_typeName);
			const bool exists = nameError.empty() && AssetRegistry::HasFile(GetNewTypePath());
			if (!m_typeName.empty() && !nameError.empty())
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
				CreateNewControlType();
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

	std::shared_ptr<EditorInstance> PcgControlTypeEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<PcgControlTypeEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open control type editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void PcgControlTypeEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	String PcgControlTypeEditor::GetNewTypePath() const
	{
		Path assetPath = m_host.GetCurrentPath();
		assetPath /= m_typeName + AssetExtension;

		// The asset registry keys files by forward-slash paths.
		String path = assetPath.string();
		std::replace(path.begin(), path.end(), '\\', '/');
		return path;
	}

	void PcgControlTypeEditor::CreateNewControlType()
	{
		const String nameError = AssetFileNameError(m_typeName);
		if (!nameError.empty())
		{
			ELOG("Invalid control type name '" << m_typeName << "': " << nameError);
			return;
		}

		const String assetPath = GetNewTypePath();
		if (AssetRegistry::HasFile(assetPath))
		{
			ELOG("Control type " << assetPath << " already exists; not overwriting it");
			return;
		}

		pcg::graph::ControlType type;
		type.name = m_typeName;
		type.shape = pcg::graph::ControlShape::ClosedLoop;
		m_typeName.clear();

		const auto file = AssetRegistry::CreateNewFile(assetPath);
		if (!file)
		{
			ELOG("Failed to create new control type " << assetPath);
			return;
		}

		*file << pcg::graph::WriteControlType(type);
		file->flush();

		// Open worlds may have looked this path up before (a placed control of a deleted or renamed type) and
		// cached "not found".
		for (WorldEditorInstance* world : WorldEditorInstance::GetOpenInstances())
		{
			if (ProceduralEditMode* mode = world->GetProceduralEditMode())
			{
				mode->GetControlLibrary().Invalidate(assetPath);
			}
		}

		ILOG("Created control type " << assetPath);
		m_host.assetImported(m_host.GetCurrentPath());

		OpenAsset(assetPath);
	}
}
