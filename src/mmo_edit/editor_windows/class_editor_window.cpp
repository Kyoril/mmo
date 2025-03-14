// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
		: EditorEntryWindowBase(project, project.classes, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Classes";
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

	void ClassEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

			int32 powerType = currentEntry.powertype();
			if (ImGui::BeginCombo("Power Type", s_powerTypes[powerType].c_str(), ImGuiComboFlags_None))
			{
				for (int i = 0; i < 3; i++)
				{
					ImGui::PushID(i);
					const bool item_selected = powerType == i;
					const char* item_text = s_powerTypes[i].c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_powertype(static_cast<proto::ClassEntry_PowerType>(i));
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

		if (ImGui::CollapsingHeader("Base Values", ImGuiTreeNodeFlags_None))
		{
			static std::vector<float> s_healthValues;
			static std::vector<float> s_manaValues;
			static std::vector<float> s_staminaValues;
			static std::vector<float> s_strengthValues;
			static std::vector<float> s_agilityValues;
			static std::vector<float> s_intellectValues;
			static std::vector<float> s_spiritValues;

			if (s_staminaValues.size() != currentEntry.levelbasevalues_size())
			{
				s_healthValues.resize(currentEntry.levelbasevalues_size());
				s_manaValues.resize(currentEntry.levelbasevalues_size());
				s_staminaValues.resize(currentEntry.levelbasevalues_size());
				s_strengthValues.resize(currentEntry.levelbasevalues_size());
				s_agilityValues.resize(currentEntry.levelbasevalues_size());
				s_intellectValues.resize(currentEntry.levelbasevalues_size());
				s_spiritValues.resize(currentEntry.levelbasevalues_size());

				for (int i = 0; i < currentEntry.levelbasevalues_size(); ++i)
				{
					s_staminaValues[i] = currentEntry.levelbasevalues(i).stamina();
					s_strengthValues[i] = currentEntry.levelbasevalues(i).strength();
					s_intellectValues[i] = currentEntry.levelbasevalues(i).intellect();
					s_agilityValues[i] = currentEntry.levelbasevalues(i).agility();
					s_spiritValues[i] = currentEntry.levelbasevalues(i).spirit();
					s_healthValues[i] = currentEntry.levelbasevalues(i).health();
					s_manaValues[i] = currentEntry.levelbasevalues(i).mana();
				}
			}

			ImGui::PlotLines("Stamina", s_staminaValues.data(), s_staminaValues.size());
			ImGui::PlotLines("Strength", s_strengthValues.data(), s_strengthValues.size());
			ImGui::PlotLines("Agility", s_agilityValues.data(), s_agilityValues.size());
			ImGui::PlotLines("Intellect", s_intellectValues.data(), s_intellectValues.size());
			ImGui::PlotLines("Spirit", s_spiritValues.data(), s_spiritValues.size());
			ImGui::PlotLines("Health", s_healthValues.data(), s_healthValues.size());
			ImGui::PlotLines("Mana", s_manaValues.data(), s_manaValues.size());

			ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);

			// Add button
			if (ImGui::Button("Add Value", ImVec2(-1, 0)))
			{
				auto* baseValues = currentEntry.add_levelbasevalues();

				if (const auto count = currentEntry.levelbasevalues_size(); count == 1)
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
					baseValues->set_health(currentEntry.levelbasevalues(count - 2).health());
					baseValues->set_mana(currentEntry.levelbasevalues(count - 2).mana());
					baseValues->set_stamina(currentEntry.levelbasevalues(count - 2).stamina());
					baseValues->set_strength(currentEntry.levelbasevalues(count - 2).strength());
					baseValues->set_agility(currentEntry.levelbasevalues(count - 2).agility());
					baseValues->set_intellect(currentEntry.levelbasevalues(count - 2).intellect());
					baseValues->set_spirit(currentEntry.levelbasevalues(count - 2).spirit());
				}
			}

			if (ImGui::BeginTable("classBaseValues", 10, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Mana", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Stamina", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Strength", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Agility", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Intellect", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Spirit", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Attribute Points", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Talent Points", ImGuiTableColumnFlags_None);
				ImGui::TableHeadersRow();

				int value = 0;

				for (int index = 0; index < currentEntry.levelbasevalues_size(); ++index)
				{
					ImGui::PushID(index);

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text("Level %d", index + 1);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->health();
					if (ImGui::InputInt("##health", &value)) currentEntry.mutable_levelbasevalues(index)->set_health(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->mana();
					if (ImGui::InputInt("##mana", &value)) currentEntry.mutable_levelbasevalues(index)->set_mana(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->stamina();
					if (ImGui::InputInt("##stamina", &value)) currentEntry.mutable_levelbasevalues(index)->set_stamina(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->strength();
					if (ImGui::InputInt("##strenght", &value)) currentEntry.mutable_levelbasevalues(index)->set_strength(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->agility();
					if (ImGui::InputInt("##agility", &value)) currentEntry.mutable_levelbasevalues(index)->set_agility(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->intellect();
					if (ImGui::InputInt("##intellect", &value)) currentEntry.mutable_levelbasevalues(index)->set_intellect(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->spirit();
					if (ImGui::InputInt("##spirit", &value)) currentEntry.mutable_levelbasevalues(index)->set_spirit(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->attributepoints();
					if (ImGui::InputInt("##ap", &value)) currentEntry.mutable_levelbasevalues(index)->set_attributepoints(value);

					ImGui::TableNextColumn();
					value = currentEntry.mutable_levelbasevalues(index)->talentpoints();
					if (ImGui::InputInt("##tp", &value)) currentEntry.mutable_levelbasevalues(index)->set_talentpoints(value);

					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			ImGui::EndChildFrame();
		}


		if (ImGui::CollapsingHeader("Experience Points", ImGuiTreeNodeFlags_None))
		{
			ImGui::PlotLines("XP to next level", [](void* data, int idx) -> float
				{
					EntryType* entry = (EntryType*)data;
					return static_cast<float>(entry->xptonextlevel(idx));
				}, &currentEntry, currentEntry.xptonextlevel_size(), 0, 0, FLT_MAX, FLT_MAX, ImVec2(0, 200));


			if (ImGui::Button("Add Value", ImVec2(-1, 0)))
			{
				currentEntry.add_xptonextlevel(currentEntry.xptonextlevel_size() == 0 ? 400 : currentEntry.xptonextlevel(currentEntry.xptonextlevel_size() - 1) * 2);
			}

			if (ImGui::BeginTable("xpToNextLevelTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("XP", ImGuiTableColumnFlags_None);
				ImGui::TableHeadersRow();

				int value = 0;

				for (int index = 0; index < currentEntry.xptonextlevel_size(); ++index)
				{
					ImGui::PushID(index);

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text("%d", index + 1);

					ImGui::TableNextColumn();
					value = currentEntry.xptonextlevel(index);
					if (ImGui::InputInt("##xp", &value)) currentEntry.set_xptonextlevel(index, value);

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		static String s_statNames[] = {
			"Stamina",
			"Strength",
			"Agility",
			"Intellect",
			"Spirit"
		};

		if (ImGui::CollapsingHeader("Attack Power", ImGuiTreeNodeFlags_None))
		{
			float offset = currentEntry.attackpoweroffset();
			if (ImGui::InputFloat("Attack Power Offset", &offset)) currentEntry.set_attackpoweroffset(offset);

			float perLevel = currentEntry.attackpowerperlevel();
			if (ImGui::InputFloat("Attack Power per Level", &perLevel)) currentEntry.set_attackpowerperlevel(perLevel);

			ImGui::Text("Attack Power Stat Source");

			// Add button
			if (ImGui::Button("Add", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_attackpowerstatsources();
				newEntry->set_statid(0);
				newEntry->set_factor(1.0f);
			}

			if (ImGui::BeginTable("statSources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.attackpowerstatsources_size(); ++index)
				{
					auto* currentSource = currentEntry.mutable_attackpowerstatsources(index);

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
						currentEntry.mutable_attackpowerstatsources()->erase(currentEntry.mutable_attackpowerstatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Health", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Health Stat Source");

			// Add button
			if (ImGui::Button("Add##addHealthStat", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_healthstatsources();
				newEntry->set_statid(0);
				newEntry->set_factor(1.0f);
			}

			if (ImGui::BeginTable("healthStatSources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.healthstatsources_size(); ++index)
				{
					auto* currentSource = currentEntry.mutable_healthstatsources(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					int statId = currentSource->statid();
					if (ImGui::Combo("##healthStat", &statId, [](void*, int index, const char** out_text) -> bool
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
					if (ImGui::InputFloat("##healthStatFactor", &factor)) currentSource->set_factor(factor);

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_healthstatsources()->erase(currentEntry.mutable_healthstatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Mana", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Mana Stat Source");

			// Add button
			if (ImGui::Button("Add##addManaStat", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_manastatsources();
				newEntry->set_statid(0);
				newEntry->set_factor(1.0f);
			}

			if (ImGui::BeginTable("manaStatSources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.manastatsources_size(); ++index)
				{
					auto* currentSource = currentEntry.mutable_manastatsources(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					int statId = currentSource->statid();
					if (ImGui::Combo("##manaStat", &statId, [](void*, int index, const char** out_text) -> bool
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
					if (ImGui::InputFloat("##manaStatFactor", &factor)) currentSource->set_factor(factor);

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_manastatsources()->erase(currentEntry.mutable_manastatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Armor", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Armor Stat Source");

			// Add button
			if (ImGui::Button("Add##addArmorStat", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_armorstatsources();
				newEntry->set_statid(0);
				newEntry->set_factor(1.0f);
			}

			if (ImGui::BeginTable("armorStatSources", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.armorstatsources_size(); ++index)
				{
					auto* currentSource = currentEntry.mutable_armorstatsources(index);

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
						currentEntry.mutable_armorstatsources()->erase(currentEntry.mutable_armorstatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Regeneration", ImGuiTreeNodeFlags_None))
		{
			float baseManaRegen = currentEntry.basemanaregenpertick();
			if (ImGui::InputFloat("Mana Regeneration per Tick", &baseManaRegen)) currentEntry.set_basemanaregenpertick(baseManaRegen);

			float spiritPerManaRegen = currentEntry.spiritpermanaregen();
			if (ImGui::InputFloat("Spirit per mana on tick", &spiritPerManaRegen)) currentEntry.set_spiritpermanaregen(spiritPerManaRegen);

			float baseHealthRegen = currentEntry.healthregenpertick();
			if (ImGui::InputFloat("Health Regeneration per Tick", &baseHealthRegen)) currentEntry.set_healthregenpertick(baseHealthRegen);

			float spiritPerHealthRegen = currentEntry.spiritperhealthregen();
			if (ImGui::InputFloat("Spirit per health on tick", &spiritPerHealthRegen)) currentEntry.set_spiritperhealthregen(spiritPerHealthRegen);
		}

		static const char* s_spellNone = "<None>";

		if (ImGui::CollapsingHeader("Spells", ImGuiTreeNodeFlags_None))
		{
			// Add button
			if (ImGui::Button("Add", ImVec2(-1, 0)))
			{
				auto* newSpell = currentEntry.add_spells();
				newSpell->set_level(1);
				newSpell->set_spell(0);
			}

			if (ImGui::BeginTable("classSpells", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.spells_size(); ++index)
				{
					auto* currentSpell = currentEntry.mutable_spells(index);

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
						currentEntry.mutable_spells()->erase(currentEntry.mutable_spells()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
	}

	void ClassEditorWindow::OnNewEntry(proto::TemplateManager<proto::Classes, proto::ClassEntry>::EntryType& entry)
	{
		entry.set_name("New class");
		entry.set_powertype(proto::ClassEntry_PowerType_MANA);
		entry.set_flags(0);
		entry.set_internalname("New class");
		entry.set_spellfamily(0);
		entry.set_attackpowerperlevel(2.0f);
		entry.set_attackpoweroffset(0.0f);

		auto* baseValues = entry.add_levelbasevalues();
		baseValues->set_health(32);
		baseValues->set_mana(110);
		baseValues->set_stamina(19);
		baseValues->set_strength(17);
		baseValues->set_agility(25);
		baseValues->set_intellect(22);
		baseValues->set_spirit(23);
	}
}
