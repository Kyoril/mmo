// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "class_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <array>
#include <algorithm>
#include <limits>
#include <cstdio>

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

		if (const auto section = ScopedEditorSection("Base Values", ImGuiTreeNodeFlags_None))
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

			ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);

			// Add button
			if (DrawSuccessButton("Add Value", ImVec2(-1, 0)))
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

		static String s_statNames[] = {
			"Stamina",
			"Strength",
			"Agility",
			"Intellect",
			"Spirit"
		};

		if (const auto section = ScopedEditorSection("Attack Power", ImGuiTreeNodeFlags_None))
		{
			float offset = currentEntry.attackpoweroffset();
			if (ImGui::InputFloat("Attack Power Offset", &offset)) currentEntry.set_attackpoweroffset(offset);

			float perLevel = currentEntry.attackpowerperlevel();
			if (ImGui::InputFloat("Attack Power per Level", &perLevel)) currentEntry.set_attackpowerperlevel(perLevel);

			ImGui::Text("Attack Power Stat Source");

			// Add button
			if (DrawSuccessButton("Add", ImVec2(-1, 0)))
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

					if (DrawDangerButton("Remove"))
					{
						currentEntry.mutable_attackpowerstatsources()->erase(currentEntry.mutable_attackpowerstatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Health", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Health Stat Source");

			// Add button
			if (DrawSuccessButton("Add##addHealthStat", ImVec2(-1, 0)))
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

					if (DrawDangerButton("Remove"))
					{
						currentEntry.mutable_healthstatsources()->erase(currentEntry.mutable_healthstatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Mana", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Mana Stat Source");

			// Add button
			if (DrawSuccessButton("Add##addManaStat", ImVec2(-1, 0)))
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

					if (DrawDangerButton("Remove"))
					{
						currentEntry.mutable_manastatsources()->erase(currentEntry.mutable_manastatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Armor", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("Armor Stat Source");

			// Add button
			if (DrawSuccessButton("Add##addArmorStat", ImVec2(-1, 0)))
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

					if (DrawDangerButton("Remove"))
					{
						currentEntry.mutable_armorstatsources()->erase(currentEntry.mutable_armorstatsources()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Regeneration", ImGuiTreeNodeFlags_None))
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

