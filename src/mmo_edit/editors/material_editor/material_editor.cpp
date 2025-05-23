// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_editor.h"

#include "material_editor_instance.h"
#include "log/default_log_levels.h"

#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/material_serializer.h"
#include "graphics/shader_compiler.h"
#include "scene_graph/material_instance_serializer.h"

namespace mmo
{
	/// @brief Default file extension for material files.
	static const String MaterialExtension = ".hmat";
	static const String MaterialFunctionExtension = ".hmf";
	
	bool MaterialEditor::CanLoadAsset(const String& extension) const
	{
		return extension == MaterialExtension || extension == MaterialFunctionExtension;
	}

	void MaterialEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New Material"))
		{
			m_showMaterialNameDialog = true;
		}
		if (ImGui::MenuItem("Create New Material Function"))
		{
			m_showMaterialFunctionNameDialog = true;
		}
	}

	void MaterialEditor::AddAssetActions(const String& asset)
	{
		// TODO: this should probably be moved to the MaterialEditorInstance class but the editor interface is not yet compatible with injecting
		// actions from other editor instances to different asset types.
		if (ImGui::MenuItem("Create Material Instance"))
		{
			m_selectedMaterial = MaterialManager::Get().Load(asset);

			m_materialInstanceName = Path(asset).filename().replace_extension().string() + "_Instance";
			m_showMaterialInstanceDialog = true;
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
			ImGui::Text(MaterialExtension.c_str());
			
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

		if (m_showMaterialFunctionNameDialog)
		{
			ImGui::OpenPopup("Create New Material Function");
			m_showMaterialFunctionNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Material Function", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material function:");

			ImGui::InputText("##field", &m_materialFunctionName);
			ImGui::SameLine();
			ImGui::Text(MaterialFunctionExtension.c_str());

			if (ImGui::Button("Create"))
			{
				CreateNewMaterialFunction();
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		if (m_showMaterialInstanceDialog)
		{
			ImGui::OpenPopup("Create Material Instance");
			m_showMaterialInstanceDialog = false;
		}

		if (ImGui::BeginPopupModal("Create Material Instance", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new material instance:");

			ImGui::InputText("##field", &m_materialInstanceName);
			ImGui::SameLine();
			ImGui::Text(".hmi");

			if (ImGui::Button("Create"))
			{
				CreateNewMaterialInstance();
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

	std::shared_ptr<EditorInstance> MaterialEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<MaterialEditorInstance>(*this, m_host, asset));
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
		currentPath /= m_materialName + MaterialExtension;
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

		const auto materialCompiler = GraphicsDevice::Get().CreateMaterialCompiler();
		const auto shaderCompiler = GraphicsDevice::Get().CreateShaderCompiler();
		materialCompiler->Compile(*material, *shaderCompiler);
		material->Update();

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		MaterialSerializer serializer;
		serializer.Export(*material, writer);

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	void MaterialEditor::CreateNewMaterialFunction()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialFunctionName + MaterialFunctionExtension;
		m_materialFunctionName.clear();

		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new material function");
			return;
		}

		// TODO: Add material function creation logic here

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	void MaterialEditor::CreateNewMaterialInstance()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_materialInstanceName + ".hmi";
		m_materialInstanceName.clear();

		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new material instance");
			return;
		}

		const auto instance = std::make_shared<MaterialInstance>(currentPath.string(), m_selectedMaterial);
		instance->SetName(currentPath.string());
		instance->DerivePropertiesFromParent();
		instance->RefreshParametersFromBase();
		instance->Update();

		io::StreamSink sink(*file);
		io::Writer writer{sink};

		MaterialInstanceSerializer serializer;
		serializer.Export(*instance, writer);

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}
}
