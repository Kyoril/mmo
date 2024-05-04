// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "spell_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/spell.h"
#include "graphics/texture_mgr.h"
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
	{
		EditorWindowBase::SetVisible(false);

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for(const auto& filename : files)
		{
			if (filename.ends_with(".htex") && filename.starts_with("Interface/Icon"))
			{
				m_textures.push_back(filename);
			}
		}
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
				spell->add_attributes(0);
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

			if (ImGui::BeginChild("spellDetails", ImVec2(-1, -1)))
			{
				if (currentSpell)
				{
					DrawSpellDetails(*currentSpell);
				}
				ImGui::EndChild();
			}

			ImGui::Columns(1);

		}
		ImGui::End();

		return false;
	}

	void SpellEditorWindow::DrawSpellDetails(proto::SpellEntry& currentSpell)
	{
#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentSpell.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentSpell.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentSpell.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentSpell.set_##name(value); \
		} \
	}
#define CHECKBOX_ATTR_PROP(index, label, flags) \
	{ \
		bool value = (currentSpell.attributes(index) & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentSpell.mutable_attributes()->Set(index, currentSpell.attributes(index) | static_cast<uint32>(flags)); \
			else \
				currentSpell.mutable_attributes()->Set(index, currentSpell.attributes(index) & ~static_cast<uint32>(flags)); \
		} \
	}
#define SLIDER_FLOAT_PROP(name, label, min, max) \
	{ \
		const char* format = "%.2f"; \
		float value = currentSpell.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_Float, &value, nullptr, nullptr)) \
		{ \
			if (value >= min && value <= max) \
				currentSpell.set_##name(value); \
		} \
	}
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)


		// Migration: Ensure spell has at least one attribute bitmap
		if (currentSpell.attributes_size() < 1)
		{
			currentSpell.add_attributes(0);
		}

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 2, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentSpell.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::BeginDisabled(true);
					String idString = std::to_string(currentSpell.id());
					ImGui::InputText("ID", &idString);
					ImGui::EndDisabled();
				}

				ImGui::EndTable();
			}

			ImGui::InputTextMultiline("Description", currentSpell.mutable_description());

			int currentSchool = currentSpell.spellschool();
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
				currentSpell.set_spellschool(currentSchool);
			}
		}

		if (ImGui::CollapsingHeader("Casting", ImGuiTreeNodeFlags_None))
		{
			SLIDER_UINT32_PROP(cost, "Cost", 0, 100000);

			SLIDER_UINT32_PROP(baselevel, "Base Level", 0, 100);
			SLIDER_UINT32_PROP(spelllevel, "Spell Level", 0, 100);
			SLIDER_UINT32_PROP(maxlevel, "Max Level", 0, 100);

			SLIDER_UINT64_PROP(cooldown, "Cooldown", 0, 1000000);
			SLIDER_UINT32_PROP(casttime, "Cast Time (ms)", 0, 100000);
			SLIDER_FLOAT_PROP(speed, "Speed (m/s)", 0, 1000);
			SLIDER_UINT32_PROP(duration, "Duration (ms)", 0, 100000);
		}

		if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_None))
		{
			CHECKBOX_ATTR_PROP(0, "Channeled", spell_attributes::Channeled);
			CHECKBOX_ATTR_PROP(0, "Ranged", spell_attributes::Ranged);
			CHECKBOX_ATTR_PROP(0, "On Next Swing", spell_attributes::OnNextSwing);
			CHECKBOX_ATTR_PROP(0, "Ability", spell_attributes::Ability);
			CHECKBOX_ATTR_PROP(0, "Trade Spell", spell_attributes::TradeSpell);
			CHECKBOX_ATTR_PROP(0, "Passive", spell_attributes::Passive);
			CHECKBOX_ATTR_PROP(0, "Hidden On Client", spell_attributes::HiddenClientSide);
			CHECKBOX_ATTR_PROP(0, "Hidden Cast Time", spell_attributes::HiddenCastTime);
			CHECKBOX_ATTR_PROP(0, "Target MainHand Item", spell_attributes::TargetMainhandItem);
			CHECKBOX_ATTR_PROP(0, "Only Daytime", spell_attributes::DaytimeOnly);
			CHECKBOX_ATTR_PROP(0, "Only Night", spell_attributes::NightOnly);
			CHECKBOX_ATTR_PROP(0, "Only Indoor", spell_attributes::IndoorOnly);
			CHECKBOX_ATTR_PROP(0, "Only Outdoor", spell_attributes::OutdoorOnly);
			CHECKBOX_ATTR_PROP(0, "Not Shapeshifted", spell_attributes::NotShapeshifted);
			CHECKBOX_ATTR_PROP(0, "Only Stealthed", spell_attributes::OnlyStealthed);
			CHECKBOX_ATTR_PROP(0, "Dont Sheath", spell_attributes::DontAffectSheathState);
			CHECKBOX_ATTR_PROP(0, "Level Damage Calc", spell_attributes::LevelDamageCalc);
			CHECKBOX_ATTR_PROP(0, "Stop Auto Attack", spell_attributes::StopAttackTarget);
			CHECKBOX_ATTR_PROP(0, "No Defense", spell_attributes::NoDefense);
			CHECKBOX_ATTR_PROP(0, "Track Target", spell_attributes::CastTrackTarget);
			CHECKBOX_ATTR_PROP(0, "Castable While Dead", spell_attributes::CastableWhileDead);
			CHECKBOX_ATTR_PROP(0, "Castable While Mounted", spell_attributes::CastableWhileMounted);
			CHECKBOX_ATTR_PROP(0, "Disabled While Active", spell_attributes::DisabledWhileActive);
			CHECKBOX_ATTR_PROP(0, "Castable While Sitting", spell_attributes::CastableWhileSitting);
			CHECKBOX_ATTR_PROP(0, "Negative", spell_attributes::Negative);
			CHECKBOX_ATTR_PROP(0, "Not In Combat", spell_attributes::NotInCombat);
			CHECKBOX_ATTR_PROP(0, "Ignore Invulnerabiltiy", spell_attributes::IgnoreInvulnerability);
			CHECKBOX_ATTR_PROP(0, "Breakable By Damage", spell_attributes::BreakableByDamage);
			CHECKBOX_ATTR_PROP(0, "Cant Cancel", spell_attributes::CantCancel);
		}

		static bool s_spellClientVisible = false;
		if (ImGui::CollapsingHeader("Client Only", ImGuiTreeNodeFlags_None))
		{
			if (!currentSpell.icon().empty())
			{
				if (!m_iconCache.contains(currentSpell.icon()))
				{
					m_iconCache[currentSpell.icon()] = TextureManager::Get().CreateOrRetrieve(currentSpell.icon());
				}

				if (const TexturePtr tex = m_iconCache[currentSpell.icon()])
				{
					ImGui::Image(tex->GetTextureObject(), ImVec2(64, 64));
				}
			}

			if (ImGui::BeginCombo("Icon", currentSpell.icon().c_str(), ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_textures.size(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_textures[i] == currentSpell.icon();
					const char* item_text = m_textures[i].c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentSpell.set_icon(item_text);
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

		if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_None))
		{
			ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);
			for (int effectIndex = 0; effectIndex < currentSpell.effects_size(); ++effectIndex)
			{
				// Effect frame
				int currentEffect = currentSpell.effects(effectIndex).type();
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
					currentSpell.mutable_effects(effectIndex)->set_type(currentEffect);
				}
				ImGui::SameLine();
				if (ImGui::Button("Details"))
				{
					ImGui::OpenPopup("SpellEffectDetails");
				}
				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					currentSpell.mutable_effects()->DeleteSubrange(effectIndex, 1);
					effectIndex--;
				}

				if (ImGui::BeginPopupModal("SpellEffectDetails", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking))
				{
					ImGui::Text("%s effect #%d", currentSpell.name().c_str(), effectIndex + 1);

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
						currentSpell.mutable_effects(effectIndex)->set_type(currentEffect);
					}

					ImGui::Text("Points");
					if (ImGui::BeginChildFrame(ImGui::GetID("effectPoints"), ImVec2(-1, 200), ImGuiWindowFlags_AlwaysUseWindowPadding))
					{
						int basePoints = currentSpell.effects(effectIndex).basepoints();
						if (ImGui::InputInt("Base Points", &basePoints))
						{
							currentSpell.mutable_effects(effectIndex)->set_basepoints(basePoints);
						}

						float pointsPerLevel = currentSpell.effects(effectIndex).pointsperlevel();
						if (ImGui::InputFloat("Per Level", &pointsPerLevel))
						{
							currentSpell.mutable_effects(effectIndex)->set_pointsperlevel(pointsPerLevel);
						}

						int diceSides = currentSpell.effects(effectIndex).diesides();
						if (ImGui::InputInt("Dice Sides", &diceSides))
						{
							currentSpell.mutable_effects(effectIndex)->set_diesides(diceSides);
						}

						float dicePerLevel = currentSpell.effects(effectIndex).diceperlevel();
						if (ImGui::InputFloat("Dice per Level", &dicePerLevel))
						{
							currentSpell.mutable_effects(effectIndex)->set_diceperlevel(dicePerLevel);
						}

						static int characterLevel = 1;
						ImGui::SliderInt("Preview Level", &characterLevel, 1, 60);

						// Calculate level scaling
						int level = characterLevel;
						if (level > currentSpell.maxlevel() && currentSpell.maxlevel() > 0)
						{
							level = currentSpell.maxlevel();
						}
						else if (level < currentSpell.baselevel())
						{
							level = currentSpell.baselevel();
						}
						level -= currentSpell.baselevel();

						ImGui::BeginDisabled(true);
						int min = basePoints + level * currentSpell.effects(effectIndex).pointsperlevel() + std::min<int>(1, diceSides + level * currentSpell.effects(effectIndex).diceperlevel());
						int max = basePoints + level * currentSpell.effects(effectIndex).pointsperlevel() + diceSides + level * currentSpell.effects(effectIndex).diceperlevel();
						ImGui::InputInt("Min", &min);
						ImGui::InputInt("Max", &max);
						ImGui::EndDisabled();

						ImGui::EndChildFrame();
					}

					if (ImGui::Button("Close"))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}

				ImGui::PopID();
			}

			// Add button
			if (ImGui::Button("Add Effect", ImVec2(-1, 0)))
			{
				currentSpell.add_effects()->set_index(currentSpell.effects_size() - 1);
			}

			ImGui::EndChildFrame();
		}
	}
}
