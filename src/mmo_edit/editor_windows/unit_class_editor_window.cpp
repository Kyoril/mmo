// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_class_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <array>
#include <algorithm>
#include <limits>
#include <cstdio>

#include "log/default_log_levels.h"

namespace ImGui
{
	void BeginGroupPanel(const char* name, const ImVec2& size = ImVec2(-1.0f, -1.0f));
	void EndGroupPanel();
}

namespace mmo
{
	namespace
	{
		static uint32 GetStatById(const proto::UnitClassBaseValues& baseValues, const int statId)
		{
			switch (statId)
			{
			case 0: return baseValues.strength();
			case 1: return baseValues.agility();
			case 2: return baseValues.stamina();
			case 3: return baseValues.intellect();
			case 4: return baseValues.spirit();
			default: return 0;
			}
		}

		template<class TSourceField>
		static uint32 CalculateStatContribution(const proto::UnitClassBaseValues& baseValues, const TSourceField& sources)
		{
			uint32 sum = 0;
			for (const auto& source : sources)
			{
				const uint32 statValue = GetStatById(baseValues, source.statid());
				const int32 effectiveValue = std::max<int32>(static_cast<int32>(statValue), 20) - 20;
				sum += static_cast<uint32>(effectiveValue * source.factor());
			}
			return sum;
		}
	}

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
		if (DrawPrimaryButton("Duplicate Unit Class"))
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
		if (const auto section = ScopedEditorSection("Basic Information", ImGuiTreeNodeFlags_DefaultOpen))
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

		// Calculated preview for quick balancing iterations
		DrawPreviewSection(currentEntry);

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
		if (const auto section = ScopedEditorSection("Base Values per Level", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextWrapped("Define base stat values for key levels. The system will interpolate between these values for intermediate levels.");

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
					s_staminaValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).stamina());
					s_strengthValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).strength());
					s_intellectValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).intellect());
					s_agilityValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).agility());
					s_spiritValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).spirit());
					s_healthValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).health());
					s_manaValues[i] = static_cast<float>(currentEntry.levelbasevalues(i).mana());
				}
			}

			static bool s_showHealth = true;
			static bool s_showMana = true;
			static bool s_showStamina = true;
			static bool s_showStrength = true;
			static bool s_showAgility = true;
			static bool s_showIntellect = true;
			static bool s_showSpirit = true;

			struct PlotSeries
			{
				const char* label;
				const std::vector<float>* values;
				bool* enabled;
				ImU32 color;
			};

			std::array<PlotSeries, 7> series = { {
				{ "Health", &s_healthValues, &s_showHealth, IM_COL32(240, 90, 90, 255) },
				{ "Mana", &s_manaValues, &s_showMana, IM_COL32(90, 140, 255, 255) },
				{ "Stamina", &s_staminaValues, &s_showStamina, IM_COL32(90, 210, 120, 255) },
				{ "Strength", &s_strengthValues, &s_showStrength, IM_COL32(255, 160, 70, 255) },
				{ "Agility", &s_agilityValues, &s_showAgility, IM_COL32(245, 235, 120, 255) },
				{ "Intellect", &s_intellectValues, &s_showIntellect, IM_COL32(180, 120, 255, 255) },
				{ "Spirit", &s_spiritValues, &s_showSpirit, IM_COL32(120, 235, 235, 255) },
			} };

			ImGui::TextUnformatted("Base Values Graph");
			for (size_t i = 0; i < series.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::Checkbox(series[i].label, series[i].enabled);
				ImGui::PopID();
				if ((i % 4) != 3 && i + 1 < series.size())
				{
					ImGui::SameLine();
				}
			}

			const ImVec2 graphSize(ImGui::GetContentRegionAvail().x, 220.0f);
			const ImVec2 graphTopLeft = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##unitClassBaseValuesCombinedGraph", graphSize);
			const ImVec2 graphBottomRight(graphTopLeft.x + graphSize.x, graphTopLeft.y + graphSize.y);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(graphTopLeft, graphBottomRight, IM_COL32(26, 29, 34, 255), 4.0f);
			drawList->AddRect(graphTopLeft, graphBottomRight, IM_COL32(82, 86, 94, 255), 4.0f);

			float minValue = std::numeric_limits<float>::max();
			float maxValue = std::numeric_limits<float>::lowest();
			int maxPoints = 0;
			for (const auto& s : series)
			{
				if (!*s.enabled || s.values->empty())
				{
					continue;
				}

				maxPoints = std::max(maxPoints, static_cast<int>(s.values->size()));
				for (const float value : *s.values)
				{
					minValue = std::min(minValue, value);
					maxValue = std::max(maxValue, value);
				}
			}

			if (maxPoints >= 2 && minValue <= maxValue)
			{
				if (minValue == maxValue)
				{
					minValue -= 1.0f;
					maxValue += 1.0f;
				}

				const float leftMargin = 52.0f;
				const float rightMargin = 12.0f;
				const float topMargin = 12.0f;
				const float bottomMargin = 24.0f;
				const float graphWidth = std::max(8.0f, graphSize.x - leftMargin - rightMargin);
				const float graphHeight = std::max(8.0f, graphSize.y - topMargin - bottomMargin);
				const ImVec2 origin(graphTopLeft.x + leftMargin, graphTopLeft.y + topMargin);

				const int yTickCount = 5;
				for (int tick = 0; tick < yTickCount; ++tick)
				{
					const float t = static_cast<float>(tick) / static_cast<float>(yTickCount - 1);
					const float y = origin.y + graphHeight - t * graphHeight;
					const float value = minValue + t * (maxValue - minValue);
					drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + graphWidth, y), IM_COL32(58, 62, 70, 255), 1.0f);
					char valueLabel[32];
					std::snprintf(valueLabel, sizeof(valueLabel), "%.0f", value);
					drawList->AddText(ImVec2(graphTopLeft.x + 6.0f, y - ImGui::GetTextLineHeight() * 0.5f), IM_COL32(190, 194, 202, 255), valueLabel);
				}

				drawList->AddLine(
					ImVec2(origin.x, origin.y + graphHeight),
					ImVec2(origin.x + graphWidth, origin.y + graphHeight),
					IM_COL32(116, 122, 134, 255),
					1.0f);

				const int levelFirst = 1;
				const int levelMid = std::max(1, maxPoints / 2);
				const int levelLast = std::max(1, maxPoints);
				const std::array<int, 3> xLevels = { levelFirst, levelMid, levelLast };
				for (const int level : xLevels)
				{
					const int pointIndex = std::clamp(level - 1, 0, maxPoints - 1);
					const float x = origin.x + (static_cast<float>(pointIndex) / static_cast<float>(maxPoints - 1)) * graphWidth;
					drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + graphHeight), IM_COL32(50, 54, 62, 255), 1.0f);
					char levelLabel[32];
					std::snprintf(levelLabel, sizeof(levelLabel), "L%d", level);
					drawList->AddText(ImVec2(x - 10.0f, origin.y + graphHeight + 3.0f), IM_COL32(190, 194, 202, 255), levelLabel);
				}

				for (const auto& s : series)
				{
					if (!*s.enabled || s.values->size() < 2)
					{
						continue;
					}

					for (size_t i = 1; i < s.values->size(); ++i)
					{
						const float x0 = origin.x + (static_cast<float>(i - 1) / static_cast<float>(s.values->size() - 1)) * graphWidth;
						const float x1 = origin.x + (static_cast<float>(i) / static_cast<float>(s.values->size() - 1)) * graphWidth;
						const float y0 = origin.y + graphHeight - ((s.values->at(i - 1) - minValue) / (maxValue - minValue)) * graphHeight;
						const float y1 = origin.y + graphHeight - ((s.values->at(i) - minValue) / (maxValue - minValue)) * graphHeight;
						drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), s.color, 2.0f);
					}
				}
			}
			else
			{
				ImGui::SetCursorScreenPos(ImVec2(graphTopLeft.x + 10.0f, graphTopLeft.y + 10.0f));
				ImGui::TextDisabled("Enable at least one series and add at least 2 levels.");
			}

			const float legendHeight = 40.0f;
			if (ImGui::BeginChild("##unitClassBaseValuesLegend", ImVec2(-1.0f, legendHeight), false))
			{
				for (size_t i = 0; i < series.size(); ++i)
				{
					ImGui::PushID(2000 + static_cast<int>(i));
					const ImVec4 color(
						static_cast<float>((series[i].color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
						static_cast<float>((series[i].color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
						static_cast<float>((series[i].color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
						*series[i].enabled ? 1.0f : 0.35f);
					ImGui::ColorButton("##legendColor", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(10.0f, 10.0f));
					ImGui::SameLine();
					if (*series[i].enabled)
					{
						ImGui::TextUnformatted(series[i].label);
					}
					else
					{
						ImGui::TextDisabled("%s", series[i].label);
					}
					ImGui::PopID();
					if ((i % 4) != 3 && i + 1 < series.size())
					{
						ImGui::SameLine();
					}
				}
			}
			ImGui::EndChild();

			// Add new level button
			if (DrawSuccessButton("Add Level"))
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

			if (ImGui::BeginTable("base_values_table", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
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
		if (const auto section = ScopedEditorSection("Stat Conversion Formulas", ImGuiTreeNodeFlags_DefaultOpen))
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
		if (const auto section = ScopedEditorSection("Regeneration Settings"))
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
		if (const auto section = ScopedEditorSection("Combat Settings"))
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
		ImGui::PushID(label);

		// Add new stat source button
		if (DrawSuccessButton("Add Source"))
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
					if (DrawDangerButton("Remove"))
					{
						statSources->erase(statSources->begin() + i);
						i--; // Adjust index since we removed an item
					}
				}

				ImGui::PopID();
			}

			ImGui::EndTable();
		}
		ImGui::PopID();

		ImGui::EndGroupPanel();
	}

	void UnitClassEditorWindow::DrawPreviewSection(EntryType& currentEntry)
	{
		if (const auto section = ScopedEditorSection("Stat Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (currentEntry.levelbasevalues_size() == 0)
			{
				ImGui::TextDisabled("Add at least one level entry in 'Base Values per Level' to see preview values.");
				return;
			}

			static int previewLevel = 1;
			previewLevel = std::clamp(previewLevel, 1, std::max(1, currentEntry.levelbasevalues_size()));
			if (ImGui::InputInt("Preview Level", &previewLevel))
			{
				previewLevel = std::clamp(previewLevel, 1, std::max(1, currentEntry.levelbasevalues_size()));
			}

			const auto& base = currentEntry.levelbasevalues(previewLevel - 1);
			const uint32 totalStamina = base.stamina();
			const uint32 totalStrength = base.strength();
			const uint32 totalAgility = base.agility();
			const uint32 totalIntellect = base.intellect();
			const uint32 totalSpirit = base.spirit();

			const uint32 totalHealth = base.health() + CalculateStatContribution(base, currentEntry.healthstatsources());
			const uint32 totalMana = base.mana() + CalculateStatContribution(base, currentEntry.manastatsources());

			uint32 attackPower = static_cast<uint32>(currentEntry.attackpoweroffset() + currentEntry.attackpowerperlevel() * static_cast<float>(previewLevel - 1));
			attackPower += CalculateStatContribution(base, currentEntry.attackpowerstatsources());

			float manaRegenPerTick = currentEntry.basemanaregenpertick();
			if (currentEntry.spiritpermanaregen() != 0.0f)
			{
				manaRegenPerTick += static_cast<float>(totalSpirit) / currentEntry.spiritpermanaregen();
			}
			manaRegenPerTick = std::max(0.0f, manaRegenPerTick);

			const float attackSpeedSec = std::max(0.001f, static_cast<float>(currentEntry.basemeleeattacktime()) / 1000.0f);
			const float damageFromAttackPower = (static_cast<float>(attackPower) * attackSpeedSec / 14.0f) * currentEntry.basemeleedamagemultiplier();
			const float assumedVariance = 0.10f;
			const float minDamage = damageFromAttackPower * (1.0f - assumedVariance);
			const float maxDamage = damageFromAttackPower * (1.0f + assumedVariance);

			if (ImGui::BeginTable("unit_class_preview_values", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Property");
				ImGui::TableSetupColumn("Value");
				ImGui::TableHeadersRow();

				auto rowUInt = [](const char* label, const uint32 value)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(label);
					ImGui::TableNextColumn();
					ImGui::Text("%u", value);
				};

				auto rowFloat = [](const char* label, const float value)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(label);
					ImGui::TableNextColumn();
					ImGui::Text("%.2f", value);
				};

				rowUInt("Total Health", totalHealth);
				rowUInt("Total Mana", totalMana);
				rowUInt("Attack Power", attackPower);
				rowFloat("Min Damage", minDamage);
				rowFloat("Max Damage", maxDamage);
				rowFloat("Mana Regen / Tick (2s)", manaRegenPerTick);

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Based on unit class formulas at selected level. Damage uses AP-derived estimate with %.0f%% variance.", assumedVariance * 100.0f);
			ImGui::TextDisabled("Primary stats: Sta %u, Str %u, Agi %u, Int %u, Spi %u", totalStamina, totalStrength, totalAgility, totalIntellect, totalSpirit);
		}
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
