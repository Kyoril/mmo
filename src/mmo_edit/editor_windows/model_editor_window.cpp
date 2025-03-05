// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "model_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ModelEditorWindow::ModelEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.models, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_toolbarButtonText = "Models";
		ReloadModelList();

		m_assetImported = m_host.assetImported.connect(this, &ModelEditorWindow::OnAssetImported);
	}

	void ModelEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (ImGui::Button("Duplicate"))
		{
			proto::ModelDataEntry* copied = m_project.models.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry.set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentEntry.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
		{
			CHECKBOX_FLAG_PROP(flags, "Customizable", model_data_flags::IsCustomizable);
			CHECKBOX_FLAG_PROP(flags, "Is Player Character", model_data_flags::IsPlayerCharacter);

			if (ImGui::BeginCombo("File", currentEntry.filename().c_str(), ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_modelFiles.size(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_modelFiles[i] == currentEntry.filename();
					const char* item_text = m_modelFiles[i].c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_filename(item_text);

						const Path p = item_text;
						if (p.extension() == ".char")
						{
							currentEntry.set_flags(currentEntry.flags() | model_data_flags::IsCustomizable);
						}
						else
						{
							currentEntry.set_flags(currentEntry.flags() & ~model_data_flags::IsCustomizable);
						}
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}
		}
	}

	void ModelEditorWindow::OnNewEntry(
		proto::TemplateManager<proto::ModelDatas, proto::ModelDataEntry>::EntryType& entry)
	{
		entry.set_filename(m_modelFiles[0].empty() ? "" : m_modelFiles[0].c_str());
	}

	void ModelEditorWindow::OnAssetImported(const Path& path)
	{
		if (path.extension() == ".hmsh" || path.extension() == ".char")
		{
			ReloadModelList();
		}
	}

	void ModelEditorWindow::ReloadModelList()
	{
		m_modelFiles.clear();

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for (const auto& filename : files)
		{
			if (filename.ends_with(".hmsh") || filename.ends_with(".char"))
			{
				m_modelFiles.push_back(filename);
			}
		}
	}
}
