// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "global_shader_parameters_editor.h"
#include "global_shader_parameters_editor_instance.h"
#include "editor_host.h"

#include <imgui.h>

#include "assets/asset_registry.h"
#include "graphics/global_shader_parameters.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "log/default_log_levels.h"

namespace mmo
{
	bool GlobalShaderParametersEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".hgsp";
	}

	void GlobalShaderParametersEditor::DrawImpl()
	{
		if (m_showNameDialog)
		{
			ImGui::OpenPopup("Create Global Shader Parameters");
			m_showNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create Global Shader Parameters", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter a name for the new global shader parameter set:");

			ImGui::InputText("##field", &m_assetName);
			ImGui::SameLine();
			ImGui::Text(".hgsp");

			if (ImGui::Button("Create"))
			{
				CreateNewAsset();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	std::shared_ptr<EditorInstance> GlobalShaderParametersEditor::OpenAssetImpl(const Path& asset)
	{
		try
		{
			auto instance = std::make_shared<GlobalShaderParametersEditorInstance>(m_host, asset);
			m_instances[asset] = instance;
			return instance;
		}
		catch (const std::exception& e)
		{
			ELOG("Failed to open global shader parameters: " << e.what());
			return nullptr;
		}
	}

	void GlobalShaderParametersEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		const auto it = m_instances.find(instance->GetAssetPath());
		if (it != m_instances.end())
		{
			m_instances.erase(it);
		}
	}

	void GlobalShaderParametersEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Global Shader Parameters"))
		{
			m_assetName = "GlobalShaderParameters";
			m_showNameDialog = true;
		}
	}

	void GlobalShaderParametersEditor::AddAssetActions(const String& asset)
	{
	}

	void GlobalShaderParametersEditor::CreateNewAsset()
	{
		if (m_assetName.empty())
		{
			return;
		}

		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_assetName + ".hgsp";

		// Persist the current (usually empty) registry state to disk as the new asset.
		if (!GlobalShaderParameters::Get().SaveToAsset(currentPath.string()))
		{
			ELOG("Failed to create global shader parameters file: " << currentPath);
			return;
		}

		m_host.assetImported(m_host.GetCurrentPath());

		OpenAsset(currentPath);

		ILOG("Created new global shader parameters: " << currentPath);
	}
}
