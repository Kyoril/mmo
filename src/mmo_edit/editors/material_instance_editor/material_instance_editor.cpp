// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_instance_editor.h"

#include "material_instance_editor_instance.h"
#include "log/default_log_levels.h"

#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "scene_graph/material_instance_serializer.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/material_serializer.h"

namespace mmo
{
	/// @brief Default file extension for material files.
	static const String MaterialInstanceExtension = ".hmi";
	
	bool MaterialInstanceEditor::CanLoadAsset(const String& extension) const
	{
		return extension == MaterialInstanceExtension;
	}

	void MaterialInstanceEditor::AddCreationContextMenuItems()
	{

	}

	void MaterialInstanceEditor::AddAssetActions(const String& asset)
	{
	}

	void MaterialInstanceEditor::DrawImpl()
	{
		if (m_showMaterialNameDialog)
		{
			ImGui::OpenPopup("Create New Material Instance");
			m_showMaterialNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Material Instance", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material instance:");

			ImGui::InputText("##field", &m_materialName);
			ImGui::SameLine();
			ImGui::Text(MaterialInstanceExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewMaterial();
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

	std::shared_ptr<EditorInstance> MaterialInstanceEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<MaterialInstanceEditorInstance>(*this, m_host, asset));
		if (!instance.second)
		{
			ELOG("Failed to open model editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void MaterialInstanceEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void MaterialInstanceEditor::CreateNewMaterial()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialName + MaterialInstanceExtension;
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
		material->Update();

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		MaterialSerializer serializer;
		serializer.Export(*material, writer);

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}
}
