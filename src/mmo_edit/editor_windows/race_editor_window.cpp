// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "race_editor_window.h"

#include "faction_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"
#include "math/constants.h"

namespace mmo
{
	RaceEditorWindow::RaceEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.races, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);
	}

	void RaceEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

#define VECTOR3_PROP(xname, yname, zname, label) \
	{ \
		float values[3] = { currentEntry.xname(), currentEntry.yname(), currentEntry.zname() }; \
		if (ImGui::InputFloat3(label, values, "%.3f", ImGuiInputTextFlags_None)) \
		{ \
			currentEntry.set_##xname(values[0]); \
			currentEntry.set_##yname(values[1]); \
			currentEntry.set_##zname(values[2]); \
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

		static const char* s_factionTemplateNone = "<None>";

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

		static const char* s_mapEntryNone = "<None>";

		if (ImGui::CollapsingHeader("Starting point", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int32 startMap = currentEntry.startmap();

			const auto* mapEntry = m_project.maps.getById(startMap);
			if (ImGui::BeginCombo("Map", mapEntry != nullptr ? mapEntry->name().c_str() : s_mapEntryNone, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.maps.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.maps.getTemplates().entry(i).id() == startMap;
					const char* item_text = m_project.maps.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_startmap(m_project.maps.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			VECTOR3_PROP(startposx, startposy, startposz, "Starting Position");
			SLIDER_FLOAT_PROP(startrotation, "Starting Rotation", 0.0f, Pi * 2.0f);
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
	}

	void RaceEditorWindow::OnNewEntry(proto::TemplateManager<proto::Races, proto::RaceEntry>::EntryType& entry)
	{
		entry.set_factiontemplate(0);
		entry.set_malemodel(0);
		entry.set_femalemodel(0);
		entry.set_baselanguage(0);
		entry.set_startingtaximask(0);
		entry.set_startmap(0);
		entry.set_startzone(0);
		entry.set_startposx(0.0f);
		entry.set_startposy(0.0f);
		entry.set_startposz(0.0f);
		entry.set_startrotation(0.0f);
		entry.set_cinematic(0);
	}
}
