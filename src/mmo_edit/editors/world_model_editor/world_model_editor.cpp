// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_editor.h"

#include "stream_sink.h"
#include "world_model_editor_instance.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "proto_data/project.h"

namespace mmo
{
	static const String WorldModelFileExtension = ".wmo";

	WorldModelEditor::WorldModelEditor(EditorHost& host, proto::Project& project)
		: EditorBase(host)
		, m_project(project)
	{
	}

	void WorldModelEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void WorldModelEditor::CreateNewWorldModelObject()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_worldModelName + WorldModelFileExtension;
		m_worldModelName.clear();
		
		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new world");
			return;
		}

		const std::shared_ptr<Material> material = std::make_shared<Material>(currentPath.string());
		material->SetType(MaterialType::Opaque);
		material->SetCastShadows(true);
		material->SetReceivesShadows(true);
		material->SetTwoSided(false);

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		// TODO: Serialize world file contents

		constexpr uint32 versionHeader = 'MVER';
		constexpr uint32 meshHeader = 'MESH';
		constexpr uint32 entityHeader = 'MENT';

		// Start writing file
		writer
			<< io::write<uint32>(versionHeader)
			<< io::write<uint32>(sizeof(uint32))
			<< io::write<uint32>(0x0001);

		// Write mesh names
		writer
			<< io::write<uint32>(meshHeader)
			<< io::write<uint32>(0);

		// TODO
		sink.Flush();


		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	bool WorldModelEditor::CanLoadAsset(const String& extension) const
	{
		return extension == WorldModelFileExtension;
	}

	void WorldModelEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New World Model Object"))
		{
			m_worldModelName.clear();
			m_showWorldModelNameDialog = true;
		}
	}

	void WorldModelEditor::DrawImpl()
	{
		if (m_showWorldModelNameDialog)
		{
			ImGui::OpenPopup("Create New World Model");
			m_showWorldModelNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New World Model", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new world:");

			ImGui::InputText("##field", &m_worldModelName);
			ImGui::SameLine();
			ImGui::Text(WorldModelFileExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewWorldModelObject();
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

	std::shared_ptr<EditorInstance> WorldModelEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<WorldModelEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open world model editor instance");
			return nullptr;
		}

		return instance.first->second;
	}
}
