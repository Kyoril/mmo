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

	// Make the UI compact because there are so many fields
	static void PushStyleCompact()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, (float)(int)(style.FramePadding.y * 0.60f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, (float)(int)(style.ItemSpacing.y * 0.60f)));
	}

	static void PopStyleCompact()
	{
		ImGui::PopStyleVar(2);
	}

	static const char* s_unknown = "UNKNOWN";

	bool ClassEditorWindow::Draw()
	{
		static std::vector<float> s_healthValues;
		static std::vector<float> s_manaValues;
		static std::vector<float> s_staminaValues;
		static std::vector<float> s_strengthValues;
		static std::vector<float> s_agilityValues;
		static std::vector<float> s_intellectValues;
		static std::vector<float> s_spiritValues;

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
				classEntry->set_attackpowerperlevel(2.0f);
				classEntry->set_attackpoweroffset(0.0f);

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

				s_staminaValues.clear();
				s_agilityValues.clear();
				s_healthValues.clear();
				s_agilityValues.clear();
				s_intellectValues.clear();
				s_spiritValues.clear();
				s_manaValues.clear();
				s_strengthValues.clear();
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
				if (ImGui::CollapsingHeader("Base Values", ImGuiTreeNodeFlags_None))
				{
					if (s_staminaValues.size() != currentClass->levelbasevalues_size())
					{
						s_healthValues.resize(currentClass->levelbasevalues_size());
						s_manaValues.resize(currentClass->levelbasevalues_size());
						s_staminaValues.resize(currentClass->levelbasevalues_size());
						s_strengthValues.resize(currentClass->levelbasevalues_size());
						s_agilityValues.resize(currentClass->levelbasevalues_size());
						s_intellectValues.resize(currentClass->levelbasevalues_size());
						s_spiritValues.resize(currentClass->levelbasevalues_size());

						for (int i = 0; i < currentClass->levelbasevalues_size(); ++i)
						{
							s_staminaValues[i] = currentClass->levelbasevalues(i).stamina();
							s_strengthValues[i] = currentClass->levelbasevalues(i).strength();
							s_intellectValues[i] = currentClass->levelbasevalues(i).intellect();
							s_agilityValues[i] = currentClass->levelbasevalues(i).agility();
							s_spiritValues[i] = currentClass->levelbasevalues(i).spirit();
							s_healthValues[i] = currentClass->levelbasevalues(i).health();
							s_manaValues[i] = currentClass->levelbasevalues(i).mana();
						}
					}

					ImGui::PlotLines("Stamina", s_staminaValues.data(), s_staminaValues.size());
					ImGui::PlotLines("Strength", s_strengthValues.data(), s_strengthValues.size());
					ImGui::PlotLines("Agility", s_agilityValues.data(), s_agilityValues.size());
					ImGui::PlotLines("Intellect", s_intellectValues.data(), s_intellectValues.size());
					ImGui::PlotLines("Spirit", s_spiritValues.data(), s_spiritValues.size());
					ImGui::PlotLines("Health", s_healthValues.data(), s_healthValues.size());
					ImGui::PlotLines("Mana", s_manaValues.data(), s_manaValues.size());

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

					ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);

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

					if (ImGui::BeginTable("classBaseValues", 8, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
					{
						ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Mana", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Stamina", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Strength", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Agility", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Intellect", ImGuiTableColumnFlags_None);
						ImGui::TableSetupColumn("Spirit", ImGuiTableColumnFlags_None);
						ImGui::TableHeadersRow();

						int value = 0;

						for (int index = 0; index < currentClass->levelbasevalues_size(); ++index)
						{
							ImGui::PushID(index);

							ImGui::TableNextRow();

							ImGui::TableNextColumn();
							ImGui::Text("Level %d", index + 1);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->health();
							if (ImGui::InputInt("##health", &value)) currentClass->mutable_levelbasevalues(index)->set_health(value);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->mana();
							if (ImGui::InputInt("##mana", &value)) currentClass->mutable_levelbasevalues(index)->set_mana(value);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->stamina();
							if (ImGui::InputInt("##stamina", &value)) currentClass->mutable_levelbasevalues(index)->set_stamina(value);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->strength();
							if (ImGui::InputInt("##strenght", &value)) currentClass->mutable_levelbasevalues(index)->set_strength(value);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->agility();
							if (ImGui::InputInt("##agility", &value)) currentClass->mutable_levelbasevalues(index)->set_agility(value);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->intellect();
							if (ImGui::InputInt("##intellect", &value)) currentClass->mutable_levelbasevalues(index)->set_intellect(value);

							ImGui::TableNextColumn();
							value = currentClass->mutable_levelbasevalues(index)->spirit();
							if (ImGui::InputInt("##spirit", &value)) currentClass->mutable_levelbasevalues(index)->set_spirit(value);

							ImGui::PopID();
						}

						ImGui::EndTable();
					}

					ImGui::EndChildFrame();
				}

				if (ImGui::CollapsingHeader("Attack Power", ImGuiTreeNodeFlags_None))
				{
					float offset = currentClass->attackpoweroffset();
					if (ImGui::InputFloat("Attack Power Offset", &offset)) currentClass->set_attackpoweroffset(offset);

					float perLevel = currentClass->attackpowerperlevel();
					if (ImGui::InputFloat("Attack Power per Level", &perLevel)) currentClass->set_attackpowerperlevel(perLevel);

					ImGui::Text("Attack Power Stat Source");

					// Add button
					if (ImGui::Button("Add", ImVec2(-1, 0)))
					{
						auto* newEntry = currentClass->add_attackpowerstatsources();
						newEntry->set_statid(0);
						newEntry->set_factor(1.0f);
					}

					if (ImGui::BeginTable("statSources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
					{
						ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_DefaultSort);
						ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableHeadersRow();

						for (int index = 0; index < currentClass->attackpowerstatsources_size(); ++index)
						{
							auto* currentSource = currentClass->mutable_attackpowerstatsources(index);

							static String s_statNames[] = {
								"Stamina",
								"Strength",
								"Agility",
								"Intellect",
								"Spirit"
							};

							ImGui::PushID(index);
							ImGui::TableNextRow();

							ImGui::TableNextColumn();

							int statId = currentSource->statid();
							if (ImGui::Combo("##stat", &statId, [](void*, int index, const char** out_text) -> bool
								{
									if (index < 0 || index > 4)
									{
										*out_text = "";
										return false;
									}

									*out_text = s_statNames[index].c_str();
									return true;
								}, nullptr, 5))
							{
								currentSource->set_statid(statId);
							}

							ImGui::TableNextColumn();

							float factor = currentSource->factor();
							if (ImGui::InputFloat("##factor", &factor)) currentSource->set_factor(factor);

							ImGui::SameLine();

							if (ImGui::Button("Remove"))
							{
								currentClass->mutable_attackpowerstatsources()->erase(currentClass->mutable_attackpowerstatsources()->begin() + index);
								index--;
							}

							ImGui::PopID();
						}

						ImGui::EndTable();
					}
				}

				static const char* s_spellNone = "<None>";

				if (ImGui::CollapsingHeader("Spells", ImGuiTreeNodeFlags_None))
				{
					// Add button
					if (ImGui::Button("Add", ImVec2(-1, 0)))
					{
						auto* newSpell = currentClass->add_spells();
						newSpell->set_level(1);
						newSpell->set_spell(0);
					}

					if (ImGui::BeginTable("classSpells", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
					{
						ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_DefaultSort);
						ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableHeadersRow();

						for (int index = 0; index < currentClass->spells_size(); ++index)
						{
							auto* currentSpell = currentClass->mutable_spells(index);

							ImGui::PushID(index);
							ImGui::TableNextRow();

							ImGui::TableNextColumn();

							int32 level = currentSpell->level();
							if (ImGui::InputInt("##level", &level))
							{
								currentSpell->set_level(level);
							}

							ImGui::TableNextColumn();

							int32 spell = currentSpell->spell();

							const auto* spellEntry = m_project.spells.getById(spell);
							if (ImGui::BeginCombo("##spell", spellEntry != nullptr ? spellEntry->name().c_str() : s_spellNone, ImGuiComboFlags_None))
							{
								for (int i = 0; i < m_project.spells.count(); i++)
								{
									ImGui::PushID(i);
									const bool item_selected = m_project.spells.getTemplates().entry(i).id() == spell;
									const char* item_text = m_project.spells.getTemplates().entry(i).name().c_str();
									if (ImGui::Selectable(item_text, item_selected))
									{
										currentSpell->set_spell(m_project.spells.getTemplates().entry(i).id());
									}
									if (item_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
									ImGui::PopID();
								}

								ImGui::EndCombo();
							}

							ImGui::SameLine();

							if (ImGui::Button("Remove"))
							{
								currentClass->mutable_spells()->erase(currentClass->mutable_spells()->begin() + index);
								index--;
							}

							ImGui::PopID();
						}

						ImGui::EndTable();
					}
				}

			}
			ImGui::EndChild();

			ImGui::Columns(1);
		}
		ImGui::End();

		return false;
	}
}
