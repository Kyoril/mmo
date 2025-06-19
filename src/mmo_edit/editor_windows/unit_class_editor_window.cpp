// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_class_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"

namespace ImGui
{
	void BeginGroupPanel(const char* name, const ImVec2& size = ImVec2(-1.0f, -1.0f));
	void EndGroupPanel();
}

namespace mmo
{
	UnitClassEditorWindow::UnitClassEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.unitClasses, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Unit Classes";
	}

	void UnitClassEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		// Duplicate button
		if (ImGui::Button("Duplicate Unit Class"))
		{
			proto::UnitClassEntry* copied = m_project.unitClasses.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
			copied->set_name(currentEntry.name() + " Copy");
		}

		// Macros for common input types
#define SLIDER_UINT32_PROP(name, label, min, max) \
		{ \
			uint32 value = currentEntry.name(); \
			if (ImGui::InputScalar(label, ImGuiDataType_U32, &value, nullptr, nullptr)) \
			{ \
				if (value >= min && value <= max) \
					currentEntry.set_##name(value); \
			} \
		}

#define SLIDER_FLOAT_PROP(name, label, min, max) \
		{ \
			float value = currentEntry.name(); \
			if (ImGui::InputFloat(label, &value)) \
			{ \
				if (value >= min && value <= max) \
					currentEntry.set_##name(value); \
			} \
		}

		// Basic Information
		if (ImGui::CollapsingHeader("Basic Information", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("basic_table", 3, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Internal Name", currentEntry.mutable_internalname());
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

			// Power Type Selection
			const char* powerTypeNames[] = { "Mana", "Rage", "Energy", "Focus", "Runic Power" };
			int currentPowerType = static_cast<int>(currentEntry.powertype());
			if (ImGui::Combo("Power Type", &currentPowerType, powerTypeNames, IM_ARRAYSIZE(powerTypeNames)))
			{
				currentEntry.set_powertype(static_cast<proto::StatConstants_PowerType>(currentPowerType));
			}
		}

		// Base Values per Level
		DrawBaseValuesSection(currentEntry);

		// Stat Sources (how stats convert to health, mana, etc.)
		DrawStatSourcesSection(currentEntry);

		// Regeneration Settings
		DrawRegenerationSection(currentEntry);

		// Combat Settings
		DrawCombatSection(currentEntry);

#undef SLIDER_UINT32_PROP
#undef SLIDER_FLOAT_PROP
	}

	void UnitClassEditorWindow::DrawBaseValuesSection(EntryType& currentEntry)
	{
		if (ImGui::CollapsingHeader("Base Values per Level", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextWrapped("Define base stat values for key levels. The system will interpolate between these values for intermediate levels.");

			// Add new level button
			if (ImGui::Button("Add Level"))
			{
				auto* newBaseValues = currentEntry.add_levelbasevalues();
				newBaseValues->set_health(100);
				newBaseValues->set_mana(50);
				newBaseValues->set_stamina(10);
				newBaseValues->set_strength(10);
				newBaseValues->set_agility(10);
				newBaseValues->set_intellect(10);
				newBaseValues->set_spirit(10);
			}

			if (ImGui::BeginTable("base_values_table", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Mana", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Stamina", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Strength", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Agility", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Intellect", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Spirit", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableHeadersRow();

				// Display existing base values
				for (int i = 0; i < currentEntry.levelbasevalues_size(); ++i)
				{
					ImGui::PushID(i);
					ImGui::TableNextRow();

					auto* baseValues = currentEntry.mutable_levelbasevalues(i);

					if (ImGui::TableNextColumn())
					{
						// Level is calculated based on index for now - could be made editable
						ImGui::Text("%d", i + 1);
					}

					if (ImGui::TableNextColumn())
					{
						uint32 health = baseValues->health();
						if (ImGui::InputScalar("##health", ImGuiDataType_U32, &health))
						{
							baseValues->set_health(health);
						}
					}

					if (ImGui::TableNextColumn())
					{
						uint32 mana = baseValues->mana();
						if (ImGui::InputScalar("##mana", ImGuiDataType_U32, &mana))
						{
							baseValues->set_mana(mana);
						}
					}

					if (ImGui::TableNextColumn())
					{
						uint32 stamina = baseValues->stamina();
						if (ImGui::InputScalar("##stamina", ImGuiDataType_U32, &stamina))
						{
							baseValues->set_stamina(stamina);
						}
					}

					if (ImGui::TableNextColumn())
					{
						uint32 strength = baseValues->strength();
						if (ImGui::InputScalar("##strength", ImGuiDataType_U32, &strength))
						{
							baseValues->set_strength(strength);
						}
					}

					if (ImGui::TableNextColumn())
					{
						uint32 agility = baseValues->agility();
						if (ImGui::InputScalar("##agility", ImGuiDataType_U32, &agility))
						{
							baseValues->set_agility(agility);
						}
					}

					if (ImGui::TableNextColumn())
					{
						uint32 intellect = baseValues->intellect();
						if (ImGui::InputScalar("##intellect", ImGuiDataType_U32, &intellect))
						{
							baseValues->set_intellect(intellect);
						}
					}

					if (ImGui::TableNextColumn())
					{
						uint32 spirit = baseValues->spirit();
						if (ImGui::InputScalar("##spirit", ImGuiDataType_U32, &spirit))
						{
							baseValues->set_spirit(spirit);
						}
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
	}

	void UnitClassEditorWindow::DrawStatSourcesSection(EntryType& currentEntry)
	{
		if (ImGui::CollapsingHeader("Stat Conversion Formulas", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextWrapped("Define how base stats (strength, agility, etc.) convert to derived stats like health, mana, attack power, and armor.");

			// Health Stat Sources
			DrawStatSourceEditor("Health Sources", currentEntry.mutable_healthstatsources());

			// Mana Stat Sources  
			DrawStatSourceEditor("Mana Sources", currentEntry.mutable_manastatsources());

			// Attack Power Stat Sources
			DrawStatSourceEditor("Attack Power Sources", currentEntry.mutable_attackpowerstatsources());

			// Armor Stat Sources
			DrawStatSourceEditor("Armor Sources", currentEntry.mutable_armorstatsources());
		}
	}

	void UnitClassEditorWindow::DrawRegenerationSection(EntryType& currentEntry)
	{
		if (ImGui::CollapsingHeader("Regeneration Settings"))
		{
			ImGui::BeginGroupPanel("Mana Regeneration");
			{
				float baseManaRegen = currentEntry.basemanaregenpertick();
				if (ImGui::InputFloat("Base Mana Regen Per Tick", &baseManaRegen))
				{
					currentEntry.set_basemanaregenpertick(baseManaRegen);
				}				float spiritToMana = currentEntry.spiritpermanaregen();
				if (ImGui::InputFloat("Spirit to Mana Factor", &spiritToMana))
				{
					currentEntry.set_spiritpermanaregen(spiritToMana);
				}
			}
			ImGui::EndGroupPanel();

			ImGui::BeginGroupPanel("Health Regeneration");
			{
				float baseHealthRegen = currentEntry.healthregenpertick();
				if (ImGui::InputFloat("Base Health Regen Per Tick", &baseHealthRegen))
				{
					currentEntry.set_healthregenpertick(baseHealthRegen);
				}

				float spiritToHealth = currentEntry.spiritperhealthregen();
				if (ImGui::InputFloat("Spirit to Health Factor", &spiritToHealth))
				{
					currentEntry.set_spiritperhealthregen(spiritToHealth);
				}
			}
			ImGui::EndGroupPanel();
		}
	}

	void UnitClassEditorWindow::DrawCombatSection(EntryType& currentEntry)
	{
		if (ImGui::CollapsingHeader("Combat Settings"))
		{
			ImGui::BeginGroupPanel("Attack Power");
			{
				float attackPowerPerLevel = currentEntry.attackpowerperlevel();
				if (ImGui::InputFloat("Attack Power Per Level", &attackPowerPerLevel))
				{
					currentEntry.set_attackpowerperlevel(attackPowerPerLevel);
				}

				float attackPowerOffset = currentEntry.attackpoweroffset();
				if (ImGui::InputFloat("Attack Power Offset", &attackPowerOffset))
				{
					currentEntry.set_attackpoweroffset(attackPowerOffset);
				}
			}
			ImGui::EndGroupPanel();

			ImGui::BeginGroupPanel("Damage Modifiers");
			{
				float meleeDamageMultiplier = currentEntry.basemeleedamagemultiplier();
				if (ImGui::InputFloat("Base Melee Damage Multiplier", &meleeDamageMultiplier))
				{
					currentEntry.set_basemeleedamagemultiplier(meleeDamageMultiplier);
				}

				float rangedDamageMultiplier = currentEntry.baserangeddamagemultiplier();
				if (ImGui::InputFloat("Base Ranged Damage Multiplier", &rangedDamageMultiplier))
				{
					currentEntry.set_baserangeddamagemultiplier(rangedDamageMultiplier);
				}
			}
			ImGui::EndGroupPanel();

			ImGui::BeginGroupPanel("Movement & Attack Speeds");
			{
				float walkSpeed = currentEntry.walkspeed();
				if (ImGui::InputFloat("Walk Speed", &walkSpeed))
				{
					currentEntry.set_walkspeed(walkSpeed);
				}

				float runSpeed = currentEntry.runspeed();
				if (ImGui::InputFloat("Run Speed", &runSpeed))
				{
					currentEntry.set_runspeed(runSpeed);
				}

				uint32 meleeAttackTime = currentEntry.basemeleeattacktime();
				if (ImGui::InputScalar("Base Melee Attack Time (ms)", ImGuiDataType_U32, &meleeAttackTime))
				{
					currentEntry.set_basemeleeattacktime(meleeAttackTime);
				}

				uint32 rangedAttackTime = currentEntry.baserangedattacktime();
				if (ImGui::InputScalar("Base Ranged Attack Time (ms)", ImGuiDataType_U32, &rangedAttackTime))
				{
					currentEntry.set_baserangedattacktime(rangedAttackTime);
				}
			}
			ImGui::EndGroupPanel();
		}
	}

	template<typename StatSourceType>
	void UnitClassEditorWindow::DrawStatSourceEditor(const char* label, google::protobuf::RepeatedPtrField<StatSourceType>* statSources)
	{
		ImGui::BeginGroupPanel(label);

		// Add new stat source button
		if (ImGui::Button("Add Source"))
		{
			auto* newSource = statSources->Add();
			newSource->set_statid(proto::StatConstants_StatType_STRENGTH);
			newSource->set_factor(1.0f);
		}

		// Display existing stat sources
		if (ImGui::BeginTable("stat_sources_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Stat", ImGuiTableColumnFlags_WidthFixed, 100.0f);
			ImGui::TableSetupColumn("Factor", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			const char* statNames[] = { "Strength", "Agility", "Stamina", "Intellect", "Spirit" };

			for (int i = 0; i < statSources->size(); ++i)
			{
				ImGui::PushID(i);
				ImGui::TableNextRow();

				auto* source = statSources->Mutable(i);

				if (ImGui::TableNextColumn())
				{
					int currentStat = static_cast<int>(source->statid());
					if (ImGui::Combo("##stat", &currentStat, statNames, IM_ARRAYSIZE(statNames)))
					{
						source->set_statid(static_cast<proto::StatConstants_StatType>(currentStat));
					}
				}

				if (ImGui::TableNextColumn())
				{
					float factor = source->factor();
					if (ImGui::InputFloat("##factor", &factor))
					{
						source->set_factor(factor);
					}
				}

				if (ImGui::TableNextColumn())
				{
					if (ImGui::Button("Remove"))
					{
						statSources->erase(statSources->begin() + i);
						i--; // Adjust index since we removed an item
					}
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::EndGroupPanel();
	}

	void UnitClassEditorWindow::OnNewEntry(proto::TemplateManager<proto::UnitClasses, proto::UnitClassEntry>::EntryType& entry)
	{
		// Set default values for a new unit class
		entry.set_name("New Unit Class");
		entry.set_internalname("UNIT_CLASS_NEW");
		entry.set_powertype(proto::StatConstants_PowerType_MANA);

		// Add a default level 1 base values entry
		auto* baseValues = entry.add_levelbasevalues();
		baseValues->set_health(100);
		baseValues->set_mana(50);
		baseValues->set_stamina(10);
		baseValues->set_strength(10);
		baseValues->set_agility(10);
		baseValues->set_intellect(10);
		baseValues->set_spirit(10);

		// Add default stat sources
		auto* healthSource = entry.add_healthstatsources();
		healthSource->set_statid(proto::StatConstants_StatType_STAMINA);
		healthSource->set_factor(10.0f);

		auto* manaSource = entry.add_manastatsources();
		manaSource->set_statid(proto::StatConstants_StatType_INTELLECT);
		manaSource->set_factor(15.0f);

		auto* attackPowerSource = entry.add_attackpowerstatsources();
		attackPowerSource->set_statid(proto::StatConstants_StatType_STRENGTH);
		attackPowerSource->set_factor(2.0f);

		auto* armorSource = entry.add_armorstatsources();
		armorSource->set_statid(proto::StatConstants_StatType_AGILITY);
		armorSource->set_factor(2.0f);

		// Set default regeneration and combat values
		entry.set_basemanaregenpertick(5.0f);
		entry.set_spiritpermanaregen(5.0f);
		entry.set_healthregenpertick(1.0f);
		entry.set_spiritperhealthregen(10.0f);

		entry.set_attackpowerperlevel(0.0f);
		entry.set_attackpoweroffset(0.0f);

		entry.set_basemeleedamagemultiplier(1.0f);
		entry.set_baserangeddamagemultiplier(1.0f);

		entry.set_walkspeed(1.0f);
		entry.set_runspeed(1.0f);
		entry.set_basemeleeattacktime(2000);
		entry.set_baserangedattacktime(2000);
	}
}
