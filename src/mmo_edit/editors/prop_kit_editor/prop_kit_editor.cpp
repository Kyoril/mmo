// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prop_kit_editor.h"

#include "prop_kit_editor_instance.h"
#include "editor_host.h"
#include "icon_font.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "scene_graph/prop_kit.h"
#include "scene_graph/prop_kit_serializer.h"

#include <imgui.h>
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace mmo
{
	static const String PropKitFileExtension = ".hkit";

	PropKitEditor::PropKitEditor(EditorHost& host, PreviewProviderManager* previewManager)
		: EditorBase(host)
		, m_previewManager(previewManager)
	{
	}

	bool PropKitEditor::CanLoadAsset(const String& extension) const
	{
		return extension == PropKitFileExtension;
	}

	void PropKitEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem(ICON_FA_CUBES " Create New Prop Kit"))
		{
			m_kitName.clear();
			m_showKitNameDialog = true;
		}
	}

	void PropKitEditor::DrawImpl()
	{
		if (m_showKitNameDialog)
		{
			ImGui::OpenPopup("Create New Prop Kit");
			m_showKitNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Prop Kit", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter a name for the new prop kit:");

			ImGui::InputText("##field", &m_kitName);
			ImGui::SameLine();
			ImGui::Text("%s", PropKitFileExtension.c_str());

			ImGui::BeginDisabled(m_kitName.empty());
			if (ImGui::Button("Create"))
			{
				CreateNewPropKit();
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

	std::shared_ptr<EditorInstance> PropKitEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<PropKitEditorInstance>(m_host, *this, asset));
		if (!instance.second)
		{
			ELOG("Failed to open prop kit editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void PropKitEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	void PropKitEditor::CreateNewPropKit()
	{
		Path assetPath = m_host.GetCurrentPath();
		assetPath /= m_kitName + PropKitFileExtension;

		PropKit kit;
		kit.name = m_kitName;
		m_kitName.clear();

		const auto file = AssetRegistry::CreateNewFile(assetPath.string());
		if (!file)
		{
			ELOG("Failed to create new prop kit " << assetPath);
			return;
		}

		io::StreamSink sink { *file };
		io::Writer writer { sink };

		PropKitSerializer serializer;
		serializer.Serialize(kit, writer);
		sink.Flush();
		file->flush();

		ILOG("Created prop kit " << assetPath);
		m_host.assetImported(m_host.GetCurrentPath());
	}
}
