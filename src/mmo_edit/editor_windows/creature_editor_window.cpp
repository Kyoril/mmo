// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "creature_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"

namespace mmo
{
	CreatureEditorWindow::CreatureEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.units, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);
	}

	void CreatureEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
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

		static const char* s_factionTemplateNone = "<None>";

		if (ImGui::CollapsingHeader("Factions", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int32 factionTemplate = currentEntry.factiontemplate();

			const auto* factionEntry = m_project.factionTemplates.getById(factionTemplate);
			if (ImGui::BeginCombo("Faction Template", factionEntry != nullptr ? factionEntry->name().c_str() : s_factionTemplateNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.factionTemplates.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.factionTemplates.getTemplates().entry(i).id() == factionTemplate;
					const char* item_text = m_project.factionTemplates.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_factiontemplate(m_project.factionTemplates.getTemplates().entry(i).id());
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

		if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int32 maleModel = currentEntry.malemodel();

			const auto* maleModelEntry = m_project.models.getById(maleModel);
			if (ImGui::BeginCombo("Male Model", maleModelEntry != nullptr ? maleModelEntry->name().c_str() : s_factionTemplateNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.models.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.models.getTemplates().entry(i).id() == maleModel;
					const char* item_text = m_project.models.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_malemodel(m_project.models.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			int32 femaleModel = currentEntry.femalemodel();

			const auto* femaleModelEntry = m_project.models.getById(femaleModel);
			if (ImGui::BeginCombo("Female Model", femaleModelEntry != nullptr ? femaleModelEntry->name().c_str() : s_factionTemplateNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.models.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.models.getTemplates().entry(i).id() == femaleModel;
					const char* item_text = m_project.models.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_femalemodel(m_project.models.getTemplates().entry(i).id());
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

		if (ImGui::CollapsingHeader("Level & Stats", ImGuiTreeNodeFlags_DefaultOpen))
		{
			SLIDER_UINT32_PROP(minlevel, "Min Level", 1, 100);
			SLIDER_UINT32_PROP(maxlevel, "Max Level", 1, 100);
			SLIDER_UINT32_PROP(minlevelhealth, "Min Level health", 1, 200000000);
			SLIDER_UINT32_PROP(maxlevelhealth, "Max Level health", 1, 200000000);
		}
	}

	void CreatureEditorWindow::OnNewEntry(proto::TemplateManager<proto::Units, proto::UnitEntry>::EntryType& entry)
	{
		entry.set_minlevel(1);
		entry.set_maxlevel(1);
		entry.set_factiontemplate(0);
		entry.set_malemodel(0);
		entry.set_femalemodel(0);
		entry.set_type(0);
		entry.set_family(0);
	}
}
