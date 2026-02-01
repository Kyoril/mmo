// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_effect_editor.h"

#include "spell_effect_editor_instance.h"
#include "log/default_log_levels.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace mmo
{
	static const String SpellEffectPreviewExtension = ".hsep";

	SpellEffectEditor::SpellEffectEditor(EditorHost& host, PreviewProviderManager& previewManager)
		: EditorBase(host)
		, m_previewManager(previewManager)
	{
	}

	void SpellEffectEditor::CloseInstanceImpl(std::shared_ptr<EditorInstance>& instance)
	{
		std::erase_if(m_instances, [&instance](const auto& item)
		{
			return item.second == instance;
		});
	}

	bool SpellEffectEditor::CanLoadAsset(const String& extension) const
	{
		return extension == SpellEffectPreviewExtension;
	}

	void SpellEffectEditor::AddCreationContextMenuItems()
	{
		if (ImGui::MenuItem("Create New Spell Effect Preview"))
		{
			m_showNameDialog = true;
		}
	}

	void SpellEffectEditor::DrawImpl()
	{
		if (m_showNameDialog)
		{
			ImGui::OpenPopup("Create New Spell Effect Preview");
			m_showNameDialog = false;
		}

		if (ImGui::BeginPopupModal("Create New Spell Effect Preview", nullptr, ImGuiWindowFlags_NoResize))
		{
			ImGui::Text("Enter a name for the new spell effect preview:");

			ImGui::InputText("##field", &m_previewName);
			ImGui::SameLine();
			ImGui::Text(SpellEffectPreviewExtension.c_str());
			
			if (ImGui::Button("Create"))
			{
				CreateNewSpellEffectPreview();
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

	std::shared_ptr<EditorInstance> SpellEffectEditor::OpenAssetImpl(const Path& asset)
	{
		const auto it = m_instances.find(asset);
		if (it != m_instances.end())
		{
			return it->second;
		}

		const auto instance = m_instances.emplace(asset, std::make_shared<SpellEffectEditorInstance>(m_host, *this, m_previewManager, asset));
		if (!instance.second)
		{
			ELOG("Failed to open spell effect editor instance");
			return nullptr;
		}

		return instance.first->second;
	}

	void SpellEffectEditor::CreateNewSpellEffectPreview()
	{
		auto currentPath = m_host.GetCurrentPath();
		currentPath /= m_previewName + SpellEffectPreviewExtension;

		// Create an empty preview file
		const auto file = AssetRegistry::CreateNewFile(currentPath.string());
		if (!file)
		{
			ELOG("Failed to create spell effect preview file: " << currentPath);
			return;
		}

		try
		{
			io::StreamSink sink(*file);
			io::Writer writer(sink);

			// Write a simple header for the preview file
			// Version
			writer << static_cast<uint32>(1);
			// Empty visualization ID (to be set later)
			writer << static_cast<uint32>(0);

			ILOG("Created new spell effect preview: " << currentPath);
		}
		catch (const std::exception& ex)
		{
			ELOG("Failed to write spell effect preview file: " << ex.what());
			return;
		}

		// Open the new file
		OpenAsset(currentPath);

		m_previewName.clear();
	}
}
