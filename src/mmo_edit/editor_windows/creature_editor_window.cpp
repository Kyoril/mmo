// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "creature_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"

namespace mmo
{
	CreatureEditorWindow::CreatureEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorWindowBase(name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);
	}

	bool CreatureEditorWindow::Draw()
	{
		if (ImGui::Begin(m_name.c_str(), &m_visible))
		{
			ImGui::Columns(2, nullptr, true);
			static bool widthSet = false;
			if (!widthSet)
			{
				ImGui::SetColumnWidth(ImGui::GetColumnIndex(), 350.0f);
				widthSet = true;
			}

			static int currentItem = -1;

			if (ImGui::Button("Add new creature", ImVec2(-1, 0)))
			{
				auto* unit = m_project.units.add();
				unit->set_name("New Creature");
				unit->set_minlevel(1);
				unit->set_maxlevel(1);
				unit->set_faction(0);
				unit->set_malemodel(0);
				unit->set_femalemodel(0);
				unit->set_type(0);
				unit->set_family(0);

			}

			ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.units.count());
			if (ImGui::Button("Remove", ImVec2(-1, 0)))
			{
				m_project.units.remove(m_project.units.getTemplates().entry().at(currentItem).id());
			}
			ImGui::EndDisabled();

			ImGui::BeginChild("creatureListScrollable", ImVec2(-1, 0));
			ImGui::ListBox("##creatureList", &currentItem, [](void* data, int idx, const char** out_text)
				{
					const proto::Units* units = static_cast<proto::Units*>(data);
					const auto& entry = units->entry().at(idx);
					*out_text = entry.name().c_str();
					return true;

				}, &m_project.units.getTemplates(), m_project.units.count(), 20);
			ImGui::EndChild();

			ImGui::NextColumn();

			proto::UnitEntry* currentEntry = nullptr;
			if (currentItem != -1 && currentItem < m_project.units.count())
			{
				currentEntry = &m_project.units.getTemplates().mutable_entry()->at(currentItem);
			}

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry->name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentEntry->set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

			ImGui::BeginChild("mapDetails", ImVec2(-1, -1));
			if (currentEntry)
			{
				ImGui::InputText("Name", currentEntry->mutable_name());

				String id = std::to_string(currentEntry->id());
				ImGui::BeginDisabled(true);
				ImGui::InputText("ID", &id);
				ImGui::EndDisabled();

			}
			ImGui::EndChild();

			ImGui::Columns(1);
		}
		ImGui::End();

		return false;
	}
}
