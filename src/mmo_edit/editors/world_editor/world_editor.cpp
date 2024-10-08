// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "world_editor.h"

#include "stream_sink.h"
#include "world_editor_instance.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace mmo
{
	static const String WorldFileExtension = ".hwld";

	WorldEditor::WorldEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	void WorldEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void WorldEditor::CreateNewWorld()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_worldName + WorldFileExtension;
		m_worldName.clear();
		
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

	bool WorldEditor::CanLoadAsset(const String& extension) const
	{
		return extension == WorldFileExtension;
	}

	void WorldEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New World"))
		{
			m_worldName.clear();
			m_showWorldNameDialog = true;
		}
	}

	void WorldEditor::DrawImpl()
	{
		if (m_showWorldNameDialog)
		{
			ImGui::OpenPopup("Create New World");
			m_showWorldNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New World", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new world:");

			ImGui::InputText("##field", &m_worldName);
			ImGui::SameLine();
			ImGui::Text(WorldFileExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewWorld();
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

	std::shared_ptr<EditorInstance> WorldEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<WorldEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open model editor instance");
			return nullptr;
		}

		return instance.first->second;
	}
}
