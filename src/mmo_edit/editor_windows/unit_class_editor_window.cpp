// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_class_editor_window.h"
#include "editor_imgui_helpers.h"
#include "attribute_ui.h"

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
		/// Default base value of every defined attribute for a new unit class level.
		constexpr uint32 DefaultAttributeBaseValue = 10;

		/// Sets every attribute defined in the project to the default base value.
		void ApplyDefaultAttributeValues(const proto::AttributeManager& attributes, proto::UnitClassBaseValues& baseValues)
		{
			for (const auto& attribute : attributes.getTemplates().entry())
			{
				if (attribute.id() < MaxAttributes)
				{
					SetAttributeValue(*baseValues.mutable_attributes(), attribute.id(), DefaultAttributeBaseValue);
				}
			}
		}

		template<class TSourceField>
		static uint32 CalculateStatContribution(const proto::UnitClassBaseValues& baseValues, const TSourceField& sources)
		{
			uint32 sum = 0;
			for (const auto& source : sources)
			{
				const uint32 statValue = source.statid() < MaxAttributes ? GetAttributeValue(baseValues.attributes(), source.statid()) : 0;
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

			static bool s_showHealth = true;
			static bool s_showMana = true;
			static std::array<bool, MaxAttributes> s_showAttribute = [] { std::array<bool, MaxAttributes> shown{}; shown.fill(true); return shown; }();

			struct PlotSeries
			{
				String label;
				std::vector<float> values;
				bool* enabled;
				ImU32 color;
			};

			// Rebuilt every frame: cheap, and never shows stale values after edits or entry switches.
			const int levelCount = currentEntry.levelbasevalues_size();
			std::vector<PlotSeries> series;
			series.reserve(2 + m_project.attributes.count());
			series.push_back({ "Health", std::vector<float>(levelCount), &s_showHealth, IM_COL32(240, 90, 90, 255) });
			series.push_back({ "Mana", std::vector<float>(levelCount), &s_showMana, IM_COL32(90, 140, 255, 255) });
			for (int i = 0; i < levelCount; ++i)
			{
				series[0].values[i] = static_cast<float>(currentEntry.levelbasevalues(i).health());
				series[1].values[i] = static_cast<float>(currentEntry.levelbasevalues(i).mana());
			}

			for (const auto& attribute : m_project.attributes.getTemplates().entry())
			{
				if (attribute.id() >= MaxAttributes)
				{
					continue;
				}

				PlotSeries attributeSeries{ attribute.name(), std::vector<float>(levelCount), &s_showAttribute[attribute.id()], GetAttributeColor(attribute.id()) };
				for (int i = 0; i < levelCount; ++i)
				{
					attributeSeries.values[i] = static_cast<float>(GetAttributeValue(currentEntry.levelbasevalues(i).attributes(), attribute.id()));
				}
				series.push_back(std::move(attributeSeries));
			}

			ImGui::TextUnformatted("Base Values Graph");
			for (size_t i = 0; i < series.size(); ++i)
			{
				ImGui::PushID(static_cast<int>(i));
				ImGui::Checkbox(series[i].label.c_str(), series[i].enabled);
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
				if (!*s.enabled || s.values.empty())
				{
					continue;
				}

				maxPoints = std::max(maxPoints, static_cast<int>(s.values.size()));
				for (const float value : s.values)
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
					if (!*s.enabled || s.values.size() < 2)
					{
						continue;
					}

					for (size_t i = 1; i < s.values.size(); ++i)
					{
						const float x0 = origin.x + (static_cast<float>(i - 1) / static_cast<float>(s.values.size() - 1)) * graphWidth;
						const float x1 = origin.x + (static_cast<float>(i) / static_cast<float>(s.values.size() - 1)) * graphWidth;
						const float y0 = origin.y + graphHeight - ((s.values.at(i - 1) - minValue) / (maxValue - minValue)) * graphHeight;
						const float y1 = origin.y + graphHeight - ((s.values.at(i) - minValue) / (maxValue - minValue)) * graphHeight;
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
						ImGui::TextUnformatted(series[i].label.c_str());
					}
					else
					{
						ImGui::TextDisabled("%s", series[i].label.c_str());
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
				if (const int count = currentEntry.levelbasevalues_size(); count > 1)
				{
					// Copy the previous level, including every attribute value
					newBaseValues->CopyFrom(currentEntry.levelbasevalues(count - 2));
				}
				else
				{
					newBaseValues->set_health(100);
					newBaseValues->set_mana(50);
					ApplyDefaultAttributeValues(m_project.attributes, *newBaseValues);
				}
			}

			const auto& attributeEntries = m_project.attributes.getTemplates().entry();
			if (ImGui::BeginTable("base_values_table", 3 + attributeEntries.size(), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Mana", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				for (const auto& attribute : attributeEntries)
				{
					ImGui::TableSetupColumn(attribute.name().c_str(), ImGuiTableColumnFlags_WidthFixed, 80.0f);
				}
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

					for (const auto& attribute : attributeEntries)
					{
						if (!ImGui::TableNextColumn())
						{
							continue;
						}

						if (attribute.id() >= MaxAttributes)
						{
							ImGui::TextDisabled("invalid id");
							continue;
						}

						ImGui::PushID(static_cast<int>(attribute.id()));
						uint32 value = GetAttributeValue(baseValues->attributes(), attribute.id());
						if (ImGui::InputScalar("##attribute", ImGuiDataType_U32, &value))
						{
							SetAttributeValue(*baseValues->mutable_attributes(), attribute.id(), value);
						}
						ImGui::PopID();
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
			ImGui::TextWrapped("Define how attributes convert to derived stats like health, mana, attack power, and armor. Each source adds (max(attribute, 20) - 20) * factor.");

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
				}

				ImGui::TextDisabled("Each source adds (attribute / points per 1) mana per tick.");
				DrawAttributeRatioSourceList("manaRegenSources", m_project.attributes, *currentEntry.mutable_manaregensources(), 5.0f);
			}
			ImGui::EndGroupPanel();

			ImGui::BeginGroupPanel("Health Regeneration");
			{
				float baseHealthRegen = currentEntry.healthregenpertick();
				if (ImGui::InputFloat("Base Health Regen Per Tick", &baseHealthRegen))
				{
					currentEntry.set_healthregenpertick(baseHealthRegen);
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

			ImGui::BeginGroupPanel("Critical Strike & Dodge");
			{
				ImGui::TextDisabled("Each source adds (attribute / points per 1) percent, on top of the base chance.");
				ImGui::TextUnformatted("Melee Critical Strike Chance Sources");
				DrawAttributeRatioSourceList("meleeCritSources", m_project.attributes, *currentEntry.mutable_meleecritsources());
				ImGui::TextUnformatted("Dodge Chance Sources");
				DrawAttributeRatioSourceList("dodgeSources", m_project.attributes, *currentEntry.mutable_dodgesources());
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

		DrawAttributeFactorSourceList("sources", m_project.attributes, *statSources);
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
			const uint32 totalHealth = base.health() + CalculateStatContribution(base, currentEntry.healthstatsources());
			const uint32 totalMana = base.mana() + CalculateStatContribution(base, currentEntry.manastatsources());

			uint32 attackPower = static_cast<uint32>(currentEntry.attackpoweroffset() + currentEntry.attackpowerperlevel() * static_cast<float>(previewLevel - 1));
			attackPower += CalculateStatContribution(base, currentEntry.attackpowerstatsources());

			// Sum of (attribute / pointsPer) over a list of ratio sources, like the server does
			auto sumRatios = [&base](const auto& sources)
			{
				float sum = 0.0f;
				for (const auto& source : sources)
				{
					if (source.attribute() < MaxAttributes && source.pointsper() != 0.0f)
					{
						sum += static_cast<float>(GetAttributeValue(base.attributes(), source.attribute())) / source.pointsper();
					}
				}
				return sum;
			};

			const float manaRegenPerTick = std::max(0.0f, sumRatios(currentEntry.manaregensources()) + currentEntry.basemanaregenpertick());
			const float critFromAttributes = sumRatios(currentEntry.meleecritsources());
			const float dodgeFromAttributes = sumRatios(currentEntry.dodgesources());

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
				rowFloat("Melee Crit % from Attributes", critFromAttributes);
				rowFloat("Dodge % from Attributes", dodgeFromAttributes);

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Based on unit class formulas at selected level. Damage uses AP-derived estimate with %.0f%% variance.", assumedVariance * 100.0f);
			String primaryStats = "Attributes:";
			for (const auto& attribute : m_project.attributes.getTemplates().entry())
			{
				if (attribute.id() < MaxAttributes)
				{
					primaryStats += " " + attribute.name() + " " + std::to_string(GetAttributeValue(base.attributes(), attribute.id())) + ",";
				}
			}
			if (primaryStats.back() == ',')
			{
				primaryStats.pop_back();
			}
			ImGui::TextDisabled("%s", primaryStats.c_str());
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
		ApplyDefaultAttributeValues(m_project.attributes, *baseValues);

		// Add default stat sources. Attribute ids of the shipped data: 0 Stamina, 1 Strength, 2 Agility,
		// 3 Intellect, 4 Spirit. Sources for attribute ids the project does not define are skipped.
		constexpr uint32 stamina = 0;
		constexpr uint32 strength = 1;
		constexpr uint32 agility = 2;
		constexpr uint32 intellect = 3;
		constexpr uint32 spirit = 4;
		const auto hasAttribute = [this](const uint32 attributeId) { return m_project.attributes.getById(attributeId) != nullptr; };

		if (hasAttribute(stamina))
		{
			auto* healthSource = entry.add_healthstatsources();
			healthSource->set_statid(stamina);
			healthSource->set_factor(10.0f);
		}

		if (hasAttribute(intellect))
		{
			auto* manaSource = entry.add_manastatsources();
			manaSource->set_statid(intellect);
			manaSource->set_factor(15.0f);
		}

		if (hasAttribute(strength))
		{
			auto* attackPowerSource = entry.add_attackpowerstatsources();
			attackPowerSource->set_statid(strength);
			attackPowerSource->set_factor(2.0f);
		}

		if (hasAttribute(agility))
		{
			auto* armorSource = entry.add_armorstatsources();
			armorSource->set_statid(agility);
			armorSource->set_factor(2.0f);

			auto* critSource = entry.add_meleecritsources();
			critSource->set_attribute(agility);
			critSource->set_pointsper(20.0f);

			auto* dodgeSource = entry.add_dodgesources();
			dodgeSource->set_attribute(agility);
			dodgeSource->set_pointsper(20.0f);
		}

		// Set default regeneration and combat values
		entry.set_basemanaregenpertick(5.0f);
		if (hasAttribute(spirit))
		{
			auto* manaRegenSource = entry.add_manaregensources();
			manaRegenSource->set_attribute(spirit);
			manaRegenSource->set_pointsper(5.0f);
		}
		entry.set_healthregenpertick(1.0f);

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
