// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "class_editor_window.h"
#include "editor_imgui_helpers.h"
#include "attribute_ui.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <array>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cstdio>

#include "assets/asset_registry.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		/// Fills the attribute base values of a class's first level with the editor defaults. The
		/// numbers are the historic level 1 defaults of the five shipped attributes (ids 0..4); any
		/// other defined attribute starts at 0.
		void ApplyDefaultAttributeValues(const proto::AttributeManager& attributes, proto::ClassBaseValues& baseValues)
		{
			static constexpr std::array<uint32, 5> s_legacyDefaults = { 19, 17, 25, 22, 23 };

			for (const auto& attribute : attributes.getTemplates().entry())
			{
				if (attribute.id() >= MaxAttributes)
				{
					continue;
				}

				const uint32 value = attribute.id() < s_legacyDefaults.size() ? s_legacyDefaults[attribute.id()] : 0;
				SetAttributeValue(*baseValues.mutable_attributes(), attribute.id(), value);
			}
		}
	}

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

		m_hasToolbarButton = false;
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

		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
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

			{
				bool disabled = currentEntry.disabled();
				if (ImGui::Checkbox("Disabled (class locked on this realm)", &disabled))
				{
					currentEntry.set_disabled(disabled);
				}
			}
			ImGui::Spacing();

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

			ImGui::Spacing();

			// Class-change spell: cast to switch the active class to this one (granted to a character
			// on creation if this is their initial class). 0 means no class-change spell yet.
			{
				static const char* s_classChangeNone = "<None>";

				const uint32 currentSpellId = currentEntry.class_change_spell();
				const auto* spellEntry = currentSpellId != 0 ? m_project.spells.getById(currentSpellId) : nullptr;
				if (ImGui::BeginCombo("Class Change Spell", spellEntry != nullptr ? spellEntry->name().c_str() : s_classChangeNone, ImGuiComboFlags_None))
				{
					if (ImGui::Selectable(s_classChangeNone, currentSpellId == 0))
					{
						currentEntry.clear_class_change_spell();
					}
					if (currentSpellId == 0)
					{
						ImGui::SetItemDefaultFocus();
					}

					for (int i = 0; i < m_project.spells.count(); i++)
					{
						ImGui::PushID(i);
						const auto& spell = m_project.spells.getTemplates().entry(i);
						const bool item_selected = spell.id() == currentSpellId;
						if (ImGui::Selectable(spell.name().c_str(), item_selected))
						{
							currentEntry.set_class_change_spell(spell.id());
						}
						if (item_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}
				ImGui::TextDisabled("Cast to switch to this class. Granted automatically as the initial class on character creation.");
			}
		}

		if (const auto section = ScopedEditorSection("Base Values", ImGuiTreeNodeFlags_None))
		{
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
			ImGui::InvisibleButton("##baseValuesCombinedGraph", graphSize);
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
			if (ImGui::BeginChild("##baseValuesLegend", ImVec2(-1.0f, legendHeight), false))
			{
				for (size_t i = 0; i < series.size(); ++i)
				{
					ImGui::PushID(1000 + static_cast<int>(i));
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

			ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);

			// Add button
			if (DrawSuccessButton("Add Value", ImVec2(-1, 0)))
			{
				auto* baseValues = currentEntry.add_levelbasevalues();

				if (const auto count = currentEntry.levelbasevalues_size(); count == 1)
				{
					baseValues->set_health(32);
					baseValues->set_mana(110);
					ApplyDefaultAttributeValues(m_project.attributes, *baseValues);
				}
				else
				{
					// Copy the previous level, including every attribute value
					baseValues->CopyFrom(currentEntry.levelbasevalues(count - 2));
				}
			}

			const auto& attributeEntries = m_project.attributes.getTemplates().entry();
			if (ImGui::BeginTable("classBaseValues", 5 + attributeEntries.size(), ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Mana", ImGuiTableColumnFlags_None);
				for (const auto& attribute : attributeEntries)
				{
					ImGui::TableSetupColumn(attribute.name().c_str(), ImGuiTableColumnFlags_None);
				}
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

					for (const auto& attribute : attributeEntries)
					{
						ImGui::TableNextColumn();
						if (attribute.id() >= MaxAttributes)
						{
							ImGui::TextDisabled("invalid id");
							continue;
						}

						ImGui::PushID(static_cast<int>(attribute.id()));
						value = static_cast<int>(GetAttributeValue(currentEntry.levelbasevalues(index).attributes(), attribute.id()));
						if (ImGui::InputInt("##attribute", &value))
						{
							SetAttributeValue(*currentEntry.mutable_levelbasevalues(index)->mutable_attributes(), attribute.id(), static_cast<uint32>(std::max(value, 0)));
						}
						ImGui::PopID();
					}

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


		if (const auto section = ScopedEditorSection("Experience Points", ImGuiTreeNodeFlags_None))
		{
			ImGui::PlotLines("XP to next level", [](void* data, int idx) -> float
				{
					EntryType* entry = (EntryType*)data;
					return static_cast<float>(entry->xptonextlevel(idx));
				}, &currentEntry, currentEntry.xptonextlevel_size(), 0, 0, FLT_MAX, FLT_MAX, ImVec2(0, 200));


			if (DrawSuccessButton("Add Value", ImVec2(-1, 0)))
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

		if (const auto section = ScopedEditorSection("Class Level Progression", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextWrapped("Class-level curve for CLASS leveling, independent of the character level tables above. The class max level equals the number of entries; an empty list freezes the class at class level 1.");

			if (currentEntry.classlevels_size() > 0)
			{
				ImGui::PlotLines("Class XP to next level", [](void* data, int idx) -> float
					{
						EntryType* entry = (EntryType*)data;
						return static_cast<float>(entry->classlevels(idx).xptonextlevel());
					}, &currentEntry, currentEntry.classlevels_size(), 0, 0, FLT_MAX, FLT_MAX, ImVec2(0, 200));
			}

			if (DrawSuccessButton("Add Level", ImVec2(-1, 0)))
			{
				auto* classLevel = currentEntry.add_classlevels();
				if (const auto count = currentEntry.classlevels_size(); count == 1)
				{
					classLevel->set_xptonextlevel(400);
				}
				else
				{
					classLevel->set_xptonextlevel(currentEntry.classlevels(count - 2).xptonextlevel() * 2);
				}
				classLevel->set_talentpoints(1);
			}

			if (ImGui::BeginTable("classLevelsTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Class Level", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("XP to next level", ImGuiTableColumnFlags_None);
				ImGui::TableSetupColumn("Talent Points", ImGuiTableColumnFlags_None);
				ImGui::TableHeadersRow();

				int value = 0;

				for (int index = 0; index < currentEntry.classlevels_size(); ++index)
				{
					ImGui::PushID(index);

					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text("%d", index + 1);

					ImGui::TableNextColumn();
					value = currentEntry.classlevels(index).xptonextlevel();
					if (ImGui::InputInt("##classxp", &value)) currentEntry.mutable_classlevels(index)->set_xptonextlevel(value);

					ImGui::TableNextColumn();
					value = currentEntry.classlevels(index).talentpoints();
					if (ImGui::InputInt("##classtp", &value)) currentEntry.mutable_classlevels(index)->set_talentpoints(value);

					ImGui::SameLine();

					if (DrawDangerButton("Remove"))
					{
						currentEntry.mutable_classlevels()->erase(currentEntry.mutable_classlevels()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		const proto::AttributeManager& attributes = m_project.attributes;

		if (const auto section = ScopedEditorSection("Attack Power", ImGuiTreeNodeFlags_None))
		{
			float offset = currentEntry.attackpoweroffset();
			if (ImGui::InputFloat("Attack Power Offset", &offset)) currentEntry.set_attackpoweroffset(offset);

			float perLevel = currentEntry.attackpowerperlevel();
			if (ImGui::InputFloat("Attack Power per Level", &perLevel)) currentEntry.set_attackpowerperlevel(perLevel);

			ImGui::Text("Attack Power Stat Source");
			DrawAttributeFactorSourceList("attackPowerStatSources", attributes, *currentEntry.mutable_attackpowerstatsources());
		}

		if (const auto section = ScopedEditorSection("Health", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Health Stat Source");
			DrawAttributeFactorSourceList("healthStatSources", attributes, *currentEntry.mutable_healthstatsources());
		}

		if (const auto section = ScopedEditorSection("Mana", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Mana Stat Source");
			DrawAttributeFactorSourceList("manaStatSources", attributes, *currentEntry.mutable_manastatsources());
		}

		if (const auto section = ScopedEditorSection("Armor", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Armor Stat Source");
			DrawAttributeFactorSourceList("armorStatSources", attributes, *currentEntry.mutable_armorstatsources());
		}

		if (const auto section = ScopedEditorSection("Block Value", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Each source adds (statValue * factor) to the shield block value, on top of equipped item block.");
			ImGui::Text("Block Value Stat Source");
			DrawAttributeFactorSourceList("blockValueStatSources", attributes, *currentEntry.mutable_blockvaluestatsources());
		}

		if (const auto section = ScopedEditorSection("Critical Block Chance", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Each source adds (statValue * factor) percent to the critical block chance, on top of the base chance from combat settings.");
			ImGui::Text("Critical Block Chance Stat Source");
			DrawAttributeFactorSourceList("critBlockChanceStatSources", attributes, *currentEntry.mutable_critblockchancestatsources());
		}

		if (const auto section = ScopedEditorSection("Regeneration", ImGuiTreeNodeFlags_None))
		{
			float baseManaRegen = currentEntry.basemanaregenpertick();
			if (ImGui::InputFloat("Mana Regeneration per Tick", &baseManaRegen)) currentEntry.set_basemanaregenpertick(baseManaRegen);

			ImGui::TextDisabled("Each source adds (attribute / points per 1) mana per tick.");
			ImGui::Text("Mana Regeneration Sources");
			DrawAttributeRatioSourceList("manaRegenSources", attributes, *currentEntry.mutable_manaregensources(), 5.0f);

			ImGui::Separator();

			float baseHealthRegen = currentEntry.healthregenpertick();
			if (ImGui::InputFloat("Health Regeneration per Tick", &baseHealthRegen)) currentEntry.set_healthregenpertick(baseHealthRegen);

			ImGui::TextDisabled("Each source adds (attribute / points per 1) health per tick.");
			ImGui::Text("Health Regeneration Sources");
			DrawAttributeRatioSourceList("healthRegenSources", attributes, *currentEntry.mutable_healthregensources(), 5.0f);
		}

		if (const auto section = ScopedEditorSection("Critical Strike & Dodge", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Each source adds (attribute / points per 1) percent, on top of the base chance.");
			ImGui::Text("Melee Critical Strike Chance Sources");
			DrawAttributeRatioSourceList("meleeCritSources", attributes, *currentEntry.mutable_meleecritsources());

			ImGui::Separator();

			ImGui::Text("Dodge Chance Sources");
			DrawAttributeRatioSourceList("dodgeSources", attributes, *currentEntry.mutable_dodgesources());
		}

		static const char* s_spellNone = "<None>";

		if (const auto section = ScopedEditorSection("Auto Attack Spells", ImGuiTreeNodeFlags_None))
		{
			// Helper lambda to draw a spell combo box for auto-attack spell fields
			auto DrawAutoAttackSpellCombo = [&](const char* label, uint32 currentSpellId, auto setter)
			{
				const auto* spellEntry = currentSpellId != 0 ? m_project.spells.getById(currentSpellId) : nullptr;
				if (ImGui::BeginCombo(label, spellEntry != nullptr ? spellEntry->name().c_str() : s_spellNone, ImGuiComboFlags_None))
				{
					// Add None option
					if (ImGui::Selectable(s_spellNone, currentSpellId == 0))
					{
						setter(0);
					}
					if (currentSpellId == 0)
					{
						ImGui::SetItemDefaultFocus();
					}

					for (int i = 0; i < m_project.spells.count(); i++)
					{
						ImGui::PushID(i);
						const auto& spell = m_project.spells.getTemplates().entry(i);
						const bool item_selected = spell.id() == currentSpellId;
						if (ImGui::Selectable(spell.name().c_str(), item_selected))
						{
							setter(spell.id());
						}
						if (item_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}
			};

			DrawAutoAttackSpellCombo(
				"Main Hand",
				currentEntry.mainhand_auto_attack_spell(),
				[&](uint32 id) { currentEntry.set_mainhand_auto_attack_spell(id); });

			DrawAutoAttackSpellCombo(
				"Off Hand",
				currentEntry.offhand_auto_attack_spell(),
				[&](uint32 id) { currentEntry.set_offhand_auto_attack_spell(id); });

			DrawAutoAttackSpellCombo(
				"Ranged",
				currentEntry.ranged_auto_attack_spell(),
				[&](uint32 id) { currentEntry.set_ranged_auto_attack_spell(id); });

			ImGui::TextDisabled("When set, auto-attack uses the spell system instead of hardcoded combat logic.");
		}

		if (const auto section = ScopedEditorSection("Spells", ImGuiTreeNodeFlags_None))
		{
			// Add button
			if (DrawSuccessButton("Add", ImVec2(-1, 0)))
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

					if (DrawDangerButton("Remove"))
					{
						currentEntry.mutable_spells()->erase(currentEntry.mutable_spells()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		static const char* s_outfitAnyRace = "<Any Race>";
		static const char* s_outfitAnyGender = "<Any>";
		static const char* s_outfitGenders[] = { "Male", "Female" };
		static const char* s_outfitDisplayNone = "<None>";

		if (const auto section = ScopedEditorSection("Character Creation Outfits", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Cosmetic only: item displays shown on the character creation preview. Grants nothing in game and is unrelated to the race's initial items.");

			if (DrawSuccessButton("Add Outfit", ImVec2(-1, 0)))
			{
				auto* newOutfit = currentEntry.add_outfits();
				newOutfit->set_race(-1);
				newOutfit->set_gender(-1);
			}

			for (int index = 0; index < currentEntry.outfits_size(); ++index)
			{
				auto* outfit = currentEntry.mutable_outfits(index);

				ImGui::PushID(index);
				ImGui::Separator();

				const auto* raceEntry = outfit->race() >= 0 ? m_project.races.getById(outfit->race()) : nullptr;

				// A race id which no longer resolves must not look like a deliberate wildcard.
				char unknownRaceLabel[64] = {};
				if (raceEntry == nullptr && outfit->race() >= 0)
				{
					std::snprintf(unknownRaceLabel, sizeof(unknownRaceLabel), "Unknown race %d", outfit->race());
				}

				const char* racePreview = s_outfitAnyRace;
				if (raceEntry != nullptr)
				{
					racePreview = raceEntry->name().c_str();
				}
				else if (outfit->race() >= 0)
				{
					racePreview = unknownRaceLabel;
				}

				if (ImGui::BeginCombo("Race", racePreview, ImGuiComboFlags_None))
				{
					if (ImGui::Selectable(s_outfitAnyRace, outfit->race() < 0))
					{
						outfit->set_race(-1);
					}

					for (int i = 0; i < m_project.races.count(); ++i)
					{
						ImGui::PushID(i);
						const auto& race = m_project.races.getTemplates().entry(i);
						const bool item_selected = static_cast<int32>(race.id()) == outfit->race();
						if (ImGui::Selectable(race.name().c_str(), item_selected))
						{
							outfit->set_race(static_cast<int32>(race.id()));
						}
						if (item_selected)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();
					}

					ImGui::EndCombo();
				}

				const int32 gender = outfit->gender();
				const char* genderLabel = (gender == 0 || gender == 1) ? s_outfitGenders[gender] : s_outfitAnyGender;
				if (ImGui::BeginCombo("Gender", genderLabel, ImGuiComboFlags_None))
				{
					if (ImGui::Selectable(s_outfitAnyGender, gender < 0))
					{
						outfit->set_gender(-1);
					}
					if (ImGui::Selectable(s_outfitGenders[0], gender == 0))
					{
						outfit->set_gender(0);
					}
					if (ImGui::Selectable(s_outfitGenders[1], gender == 1))
					{
						outfit->set_gender(1);
					}

					ImGui::EndCombo();
				}

				String animation = outfit->animation();
				if (ImGui::InputText("Ready Animation", &animation))
				{
					outfit->set_animation(animation);
				}
				ImGui::SameLine();
				DrawHelpMarker("Animation state played while the outfit is shown, for example 1HReady, 2HReady or 2HLReady. Leave empty to keep the idle pose.");

				if (DrawSuccessButton("Add Item Display", ImVec2(-1, 0)))
				{
					outfit->add_item_displays(0);
				}

				for (int displayIndex = 0; displayIndex < outfit->item_displays_size(); ++displayIndex)
				{
					ImGui::PushID(displayIndex);

					const uint32 displayId = outfit->item_displays(displayIndex);
					const auto* displayEntry = m_project.itemDisplays.getById(displayId);

					// A display id which no longer resolves must not look like a deliberate empty slot.
					char unknownDisplayLabel[64] = {};
					if (displayEntry == nullptr && displayId != 0)
					{
						std::snprintf(unknownDisplayLabel, sizeof(unknownDisplayLabel), "Unknown display %u", displayId);
					}

					const char* displayPreview = s_outfitDisplayNone;
					if (displayEntry != nullptr)
					{
						displayPreview = displayEntry->name().c_str();
					}
					else if (displayId != 0)
					{
						displayPreview = unknownDisplayLabel;
					}

					if (ImGui::BeginCombo("##itemDisplay", displayPreview, ImGuiComboFlags_None))
					{
						const bool comboAppearing = ImGui::IsWindowAppearing();
						if (comboAppearing)
						{
							m_itemDisplayFilter.clear();
							ImGui::SetKeyboardFocusHere();
						}

						ImGui::InputText("##itemDisplayFilter", &m_itemDisplayFilter);

						for (int i = 0; i < m_project.itemDisplays.count(); ++i)
						{
							const auto& display = m_project.itemDisplays.getTemplates().entry(i);

							if (!m_itemDisplayFilter.empty())
							{
								const String& name = display.name();
								const auto matchIt = std::search(
									name.begin(), name.end(),
									m_itemDisplayFilter.begin(), m_itemDisplayFilter.end(),
									[](const unsigned char lhs, const unsigned char rhs)
									{
										return std::tolower(lhs) == std::tolower(rhs);
									});

								if (matchIt == name.end())
								{
									continue;
								}
							}

							ImGui::PushID(i);
							const bool item_selected = display.id() == displayId;
							if (ImGui::Selectable(display.name().c_str(), item_selected))
							{
								outfit->set_item_displays(displayIndex, display.id());
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

					if (DrawDangerButton("Remove"))
					{
						outfit->mutable_item_displays()->erase(outfit->mutable_item_displays()->begin() + displayIndex);
						displayIndex--;
					}

					ImGui::PopID();
				}

				if (DrawDangerButton("Remove Outfit"))
				{
					currentEntry.mutable_outfits()->erase(currentEntry.mutable_outfits()->begin() + index);
					index--;
				}

				ImGui::PopID();
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
		ApplyDefaultAttributeValues(m_project.attributes, *baseValues);

		// 1% melee crit and dodge per 20 points of the attribute in slot 2 (Agility in the shipped
		// data), as every class had before these became data. Skipped if that slot is not defined.
		constexpr uint32 critAndDodgeAttribute = 2;
		if (m_project.attributes.getById(critAndDodgeAttribute) != nullptr)
		{
			auto* crit = entry.add_meleecritsources();
			crit->set_attribute(critAndDodgeAttribute);
			crit->set_pointsper(20.0f);

			auto* dodge = entry.add_dodgesources();
			dodge->set_attribute(critAndDodgeAttribute);
			dodge->set_pointsper(20.0f);
		}
	}
}
