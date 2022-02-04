// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_editor.h"

#include "material_editor_instance.h"
#include "log/default_log_levels.h"

#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/material_serializer.h"

namespace mmo
{
	bool MaterialEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".hmat";
	}

	void MaterialEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New Material"))
		{
			m_showMaterialNameDialog = true;
		}
	}

	void MaterialEditor::DrawImpl()
	{
		if (m_showMaterialNameDialog)
		{
			ImGui::OpenPopup("Create New Material");
			m_showMaterialNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Material", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material:");

			ImGui::InputText("##field", &m_materialName);
			ImGui::SameLine();
			ImGui::Text(".hmat");
			
			ImGui::TableSetColumnIndex(1);
			if (ImGui::Button("Create"))
			{
				CreateNewMaterial();
				ImGui::CloseCurrentPopup();
			}
			
			ImGui::TableSetColumnIndex(2);
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	std::shared_ptr<EditorInstance> MaterialEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<MaterialEditorInstance>(m_host, asset));
		if (!instance.second)
		{
			ELOG("Failed to open model editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void MaterialEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void MaterialEditor::CreateNewMaterial()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialName + ".hmat";
		m_materialName.clear();
		
		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new material");
			return;
		}

		const std::shared_ptr<Material> material = std::make_shared<Material>(currentPath.string());
		material->SetType(MaterialType::Opaque);
		material->SetCastShadows(true);
		material->SetReceivesShadows(true);
		material->SetTwoSided(false);

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		MaterialSerializer serializer;
		serializer.Export(*material, writer);

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}
}
