// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "character_editor.h"

#include "character_editor_instance.h"
#include "stream_sink.h"
#include "assets/asset_registry.h"
#include "log/default_log_levels.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace mmo
{
	CharacterEditor::CharacterEditor(EditorHost& host)
		: EditorBase(host)
	{
	}

	void CharacterEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	bool CharacterEditor::CanLoadAsset(const String& extension) const
	{
		return extension == ".char";
	}

	void CharacterEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New Character Data"))
		{
			m_showNameDialog = true;
		}
	}

	void CharacterEditor::AddAssetActions(const String& asset)
	{
		EditorBase::AddAssetActions(asset);
	}

	void CharacterEditor::CreateNewCharacterData()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_dataName + ".char";
		m_dataName.clear();

		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create new character data");
			return;
		}

		// TODO: Setup instance

		io::StreamSink sink{ *file };
		io::Writer writer{ sink };

		// TODO: Write content

		file->flush();

		m_host.assetImported(m_host.GetCurrentPath());
	}

	void CharacterEditor::DrawImpl()
	{
		if (m_showNameDialog)
		{
			ImGui::OpenPopup("Create New Character Data");
			m_showNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Character Data", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new character data:");

			ImGui::InputText("##field", &m_dataName);
			ImGui::SameLine();
			ImGui::Text(".char");

			if (ImGui::Button("Create"))
			{
				CreateNewCharacterData();
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

	std::shared_ptr<EditorInstance> CharacterEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<CharacterEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open mesh editor instance");
			return nullptr;
		}

		return instance.first->second;
	}
}
