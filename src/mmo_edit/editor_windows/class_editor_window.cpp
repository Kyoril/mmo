// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "class_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static String s_powerTypes[] = {
		"Mana",
		"Rage",
		"Energy"
	};

	ClassEditorWindow::ClassEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorWindowBase(name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);
	}

	bool ClassEditorWindow::Draw()
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

			if (ImGui::Button("Add new class", ImVec2(-1, 0)))
			{
				auto* classEntry = m_project.classes.add();
				classEntry->set_name("New class");
				classEntry->set_powertype(proto::ClassEntry_PowerType_MANA);
				classEntry->set_flags(0);
				classEntry->set_internalname("New class");
				classEntry->set_spellfamily(0);
				auto* baseValues = classEntry->add_levelbasevalues();
				baseValues->set_health(32);
				baseValues->set_mana(110);
				baseValues->set_stamina(19);
				baseValues->set_strength(17);
				baseValues->set_agility(25);
				baseValues->set_intellect(22);
				baseValues->set_spirit(23);
			}

			ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.classes.count());
			if (ImGui::Button("Remove", ImVec2(-1, 0)))
			{
				m_project.classes.remove(m_project.classes.getTemplates().entry().at(currentItem).id());
			}
			ImGui::EndDisabled();

			ImGui::BeginChild("classListScrollable", ImVec2(-1, 0));
			ImGui::ListBox("##classList", &currentItem, [](void* data, int idx, const char** out_text)
				{
					const proto::Classes* classes = static_cast<proto::Classes*>(data);
					const auto& entry = classes->entry().at(idx);
					*out_text = entry.name().c_str();
					return true;

				}, &m_project.classes.getTemplates(), m_project.classes.count(), 20);
			ImGui::EndChild();

			ImGui::NextColumn();

			proto::ClassEntry* currentClass = nullptr;
			if (currentItem != -1 && currentItem < m_project.classes.count())
			{
				currentClass = &m_project.classes.getTemplates().mutable_entry()->at(currentItem);
			}

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentSpell->name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentSpell->set_##name(value); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentSpell->name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentSpell->set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

			ImGui::BeginChild("classDetails", ImVec2(-1, -1));
			if (currentClass)
			{
				if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
				{
					if (ImGui::TableNextColumn())
					{
						ImGui::InputText("Name", currentClass->mutable_name());
					}
					
					if (ImGui::TableNextColumn())
					{
						ImGui::BeginDisabled(true);
						String idString = std::to_string(currentClass->id());
						ImGui::InputText("ID", &idString);
						ImGui::EndDisabled();
					}

					ImGui::EndTable();
				}

				ImGui::Text("Base Values");
				ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);
				for (int index = 0; index < currentClass->levelbasevalues_size(); ++index)
				{
					// Effect frame
					ImGui::PushID(index);

					if (ImGui::Button("Details"))
					{
						ImGui::OpenPopup("SpellEffectDetails");
					}
					ImGui::SameLine();
					if (ImGui::Button("Remove"))
					{
						currentClass->mutable_levelbasevalues()->DeleteSubrange(index, 1);
						index--;
					}

					ImGui::PopID();
				}

				// Add button
				if (ImGui::Button("Add Value", ImVec2(-1, 0)))
				{
					auto* baseValues = currentClass->add_levelbasevalues();

					if (const auto count = currentClass->levelbasevalues_size(); count == 1)
					{
						baseValues->set_health(32);
						baseValues->set_mana(110);
						baseValues->set_stamina(19);
						baseValues->set_strength(17);
						baseValues->set_agility(25);
						baseValues->set_intellect(22);
						baseValues->set_spirit(23);
					}
					else
					{
						baseValues->set_health(currentClass->levelbasevalues(count - 2).health());
						baseValues->set_mana(currentClass->levelbasevalues(count - 2).mana());
						baseValues->set_stamina(currentClass->levelbasevalues(count - 2).stamina());
						baseValues->set_strength(currentClass->levelbasevalues(count - 2).strength());
						baseValues->set_agility(currentClass->levelbasevalues(count - 2).agility());
						baseValues->set_intellect(currentClass->levelbasevalues(count - 2).intellect());
						baseValues->set_spirit(currentClass->levelbasevalues(count - 2).spirit());
					}
				}
				ImGui::EndChildFrame();
			}
			ImGui::EndChild();

			ImGui::Columns(1);

		}
		ImGui::End();

		return false;
	}
}
