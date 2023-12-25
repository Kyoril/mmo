// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "spell_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "log/default_log_levels.h"

namespace mmo
{
	static String s_spellSchoolNames[] = {
		"Physical",
		"Holy",
		"Fire",
		"Nature",
		"Frost",
		"Shadow",
		"Arcane"
	};

	static String s_spellEffectNames[] = {
		"None",
		"Instakill",
		"School Damage",
		"Dummy",
		"Portal Teleport",
		"Teleport Units",
		"Apply Aura",
		"Power Drain",
		"Health Leech",
		"Heal",
		"Bind",
		"Portal",
		"Quest Complete",
		"Weapon Damage + (noschool)",
		"Resurrect",
		"Extra Attacks",
		"Dodge",
		"Evade",
		"Parry",
		"Block",
		"Create Item",
		"Weapon",
		"Defense",
		"Persistent Area Aura",
		"Summon",
		"Leap",
		"Energize",
		"Weapon % Dmg",
		"Trigger Missile",
		"Open Lock",
		"Learn Spell",
		"Weapon Damage +"
	};

	SpellEditorWindow::SpellEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorWindowBase(name)
		, m_host(host)
		, m_project(project)
		, m_visible(false)
	{
	}

	bool SpellEditorWindow::Draw()
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

			if (ImGui::Button("Add new spell", ImVec2(-1, 0)))
			{
				auto* spell = m_project.spells.add();
				spell->set_name("New spell");
			}

			ImGui::BeginDisabled(currentItem == -1 || currentItem >= m_project.spells.count());
			if (ImGui::Button("Remove", ImVec2(-1, 0)))
			{
				m_project.spells.remove(m_project.spells.getTemplates().entry().at(currentItem).id());
			}
			ImGui::EndDisabled();

			ImGui::BeginChild("spellListScrollable", ImVec2(-1, 0));
			ImGui::ListBox("##spellList", &currentItem, [](void* data, int idx, const char** out_text)
				{
					const proto::Spells* spells = static_cast<proto::Spells*>(data);
					const auto& entry = spells->entry().at(idx);
					*out_text = entry.name().c_str();
					return true;

				}, &m_project.spells.getTemplates(), m_project.spells.count(), 20);
			ImGui::EndChild();

			ImGui::NextColumn();

			proto::SpellEntry* currentSpell = nullptr;
			if (currentItem != -1 && currentItem < m_project.spells.count())
			{
				currentSpell = &m_project.spells.getTemplates().mutable_entry()->at(currentItem);
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
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

			ImGui::BeginChild("spellDetails", ImVec2(-1, -1));
			if (currentSpell)
			{
				ImGui::InputText("Name", currentSpell->mutable_name());

				SLIDER_UINT32_PROP(cost, "Cost", 0, 100000);
				SLIDER_UINT64_PROP(cooldown, "Cooldown", 0, 1000000);

				int currentSchool = currentSpell->spellschool();
				if (ImGui::Combo("Spell School", &currentSchool, [](void*, int idx, const char** out_text)
					{
						if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellSchoolNames))
						{
							return false;
						}

						*out_text = s_spellSchoolNames[idx].c_str();
						return true;
					}, nullptr, IM_ARRAYSIZE(s_spellSchoolNames), -1))
				{
					currentSpell->set_spellschool(currentSchool);
				}

				ImGui::Text("Effects");
				ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 0), ImGuiWindowFlags_AlwaysUseWindowPadding);
				for (int effectIndex = 0; effectIndex < currentSpell->effects_size(); ++effectIndex)
				{
					// Effect frame
					int currentEffect = currentSpell->effects(effectIndex).type();
					ImGui::PushID(effectIndex);
					if (ImGui::Combo("Effect", &currentEffect,
						[](void* data, int idx, const char** out_text)
						{
							if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellEffectNames))
							{
								return false;
							}

							*out_text = s_spellEffectNames[idx].c_str();
							return true;
						}, nullptr, IM_ARRAYSIZE(s_spellEffectNames)))
					{
						currentSpell->mutable_effects(effectIndex)->set_type(currentEffect);
					}
					ImGui::PopID();
					ImGui::SameLine();
					ImGui::Button("Details");
				}
				// Add button
				if (ImGui::Button("Add Effect", ImVec2(-1, 0)))
				{
					currentSpell->add_effects()->set_index(currentSpell->effects_size() - 1);
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
