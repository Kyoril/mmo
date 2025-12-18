// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "quest_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/object_type_id.h"
#include "game/quest.h"
#include "log/default_log_levels.h"
#include "math/clamp.h"

namespace mmo
{
	namespace
	{
		float GetBasePercent(const int32 questLevel)
		{
			if (questLevel <= 5) return 0.17f;
			if (questLevel <= 10) return 0.13f;
			if (questLevel <= 15) return 0.09f;
			return 0.06f;
		}

		int32 GetXPToNextLevel(const int32 questLevel)
		{
			// TODO: Derive from table
			int32 xpToNextLevel[] = 
			{
				400,
				900,
				1400,
				2100,
				2800,
				3600,
				4500,
				5400,
				6500,
				7600,
				8800,
				10100,
				11400,
				12900,
				14400,
				16000,
				17700,
				19400,
				21300,
				23200
			};

			if (questLevel < 0)
			{
				return xpToNextLevel[0];
			}

			if (questLevel >= std::size(xpToNextLevel))
			{
				return xpToNextLevel[std::size(xpToNextLevel) - 1];
			}

			return xpToNextLevel[questLevel];
		}

		int32 RoundToNearest5(const float value)
		{
			return static_cast<int>((value + 2.5f) / 5.0f) * 5;
		}

		int32 GetSuggestedQuestXP(const int32 questLevel, const float difficultyMultiplier)
		{
			const int32 xpToNextLevel = GetXPToNextLevel(questLevel);
			const float basePercent = GetBasePercent(questLevel);
			const float baseReward = xpToNextLevel * basePercent * difficultyMultiplier;
			return RoundToNearest5(baseReward);
		}

		void DrawSectionHeader(const char* text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
			ImGui::Text("%s", text);
			ImGui::PopStyleColor();
			ImGui::Separator();
			ImGui::Spacing();
		}

		void DrawHelpMarker(const char* desc)
		{
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(desc);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

	}

	QuestEditorWindow::QuestEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.quests, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Quests";
	}

	void QuestEditorWindow::DrawDetailsImpl(proto::QuestEntry& currentEntry)
	{
		// Top toolbar with actions
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		if (ImGui::Button("Duplicate Quest", ImVec2(120, 0)))
		{
			proto::QuestEntry* copied = m_project.quests.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}
		ImGui::PopStyleColor();
		
		ImGui::SameLine();
		DrawHelpMarker("Create a copy of this quest with a new ID");

		ImGui::Separator();
		ImGui::Spacing();

#define SLIDER_UNSIGNED_PROP(name, label, datasize, min, max) \
	{ \
		const char* format = "%d"; \
		uint##datasize value = currentEntry.name(); \
		if (ImGui::InputScalar(label, ImGuiDataType_U##datasize, &value, nullptr, nullptr)) \
		{ \
			if (value >= (min) && value <= (max)) \
				currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_BOOL_PROP(name, label) \
	{ \
		bool value = currentEntry.name(); \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			currentEntry.set_##name(value); \
		} \
	}
#define CHECKBOX_FLAG_PROP(property, label, flags) \
	{ \
		bool value = (currentEntry.property() & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.set_##property(currentEntry.property() | static_cast<uint32>(flags)); \
			else \
				currentEntry.set_##property(currentEntry.property() & ~static_cast<uint32>(flags)); \
		} \
	}
#define CHECKBOX_ATTR_PROP(index, label, flags) \
	{ \
		bool value = (currentEntry.attributes(index) & static_cast<uint32>(flags)) != 0; \
		if (ImGui::Checkbox(label, &value)) \
		{ \
			if (value) \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) | static_cast<uint32>(flags)); \
			else \
				currentEntry.mutable_attributes()->Set(index, currentEntry.attributes(index) & ~static_cast<uint32>(flags)); \
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

		if (ImGui::CollapsingHeader("Quest Information", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
			
			DrawSectionHeader("Basic Details");
			
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputText("##InternalName", currentEntry.mutable_internalname());
			ImGui::SameLine();
			ImGui::Text("Internal Name");
			ImGui::SameLine();
			DrawHelpMarker("Internal identifier used for development");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputText("##QuestTitle", currentEntry.mutable_name());
			ImGui::SameLine();
			ImGui::Text("Quest Title");
			ImGui::SameLine();
			DrawHelpMarker("The title shown to players");

			ImGui::BeginDisabled(true);
			String idString = std::to_string(currentEntry.id());
			ImGui::SetNextItemWidth(100);
			ImGui::InputText("##ID", &idString);
			ImGui::SameLine();
			ImGui::Text("Quest ID");
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Level Requirements");

			// Level settings in a compact grid
			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(questlevel, "Quest Level", 0, 255);
			ImGui::SameLine();
			DrawHelpMarker("The level of the quest itself");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(minlevel, "Min Level", 0, 255);
			ImGui::SameLine();
			DrawHelpMarker("Minimum player level required");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(maxlevel, "Max Level", 0, 255);
			ImGui::SameLine();
			DrawHelpMarker("Maximum player level allowed (0 = no limit)");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Quest Type");

			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
			if (ImGui::RadioButton("Turn In", currentEntry.type() == 0))
			{
				currentEntry.set_type(0);
			}
			ImGui::SameLine();
			DrawHelpMarker("Simple delivery quest");
			
			ImGui::SameLine(0, 20);
			if (ImGui::RadioButton("Task", currentEntry.type() == 1))
			{
				currentEntry.set_type(1);
			}
			ImGui::SameLine();
			DrawHelpMarker("Quest with specific tasks");
			
			ImGui::SameLine(0, 20);
			if (ImGui::RadioButton("Quest", currentEntry.type() == 2))
			{
				currentEntry.set_type(2);
			}
			ImGui::SameLine();
			DrawHelpMarker("Full quest with objectives");
			ImGui::PopStyleColor();

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Quest Flags");

			// Quest flags in a grid
			if (ImGui::BeginTable("questFlags", 2, ImGuiTableFlags_None))
			{
				ImGui::TableNextColumn();
				CHECKBOX_FLAG_PROP(flags, "Stay Alive", quest_flags::StayAlive);
				ImGui::SameLine();
				DrawHelpMarker("Quest fails if player dies");

				ImGui::TableNextColumn();
				CHECKBOX_FLAG_PROP(flags, "Party Accept", quest_flags::PartyAccept);
				ImGui::SameLine();
				DrawHelpMarker("All party members can accept");

				ImGui::TableNextColumn();
				CHECKBOX_FLAG_PROP(flags, "Can Be Shared", quest_flags::Sharable);
				ImGui::SameLine();
				DrawHelpMarker("Quest can be shared with party members");

				ImGui::TableNextColumn();
				CHECKBOX_FLAG_PROP(flags, "Exploration", quest_flags::Exploration);
				ImGui::SameLine();
				DrawHelpMarker("Exploration-based quest");

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Quest Items & Prerequisites");

			uint32 sourceItemId = currentEntry.srcitemid();
			const auto* itemEntry = m_project.items.getById(sourceItemId);
			
			ImGui::SetNextItemWidth(300);
			if (ImGui::BeginCombo("##InitialItem", itemEntry != nullptr ? itemEntry->name().c_str() : "None", ImGuiComboFlags_None))
			{
				ImGui::PushID(0);
				if (ImGui::Selectable("None"))
				{
					currentEntry.set_srcitemid(0);
					sourceItemId = 0;
				}
				ImGui::PopID();
			
				for (int i = 0; i < m_project.items.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.items.getTemplates().entry(i).id() == sourceItemId;
					const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_srcitemid(m_project.items.getTemplates().entry(i).id());
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
			ImGui::Text("Initial Quest Item");
			ImGui::SameLine();
			DrawHelpMarker("Item given to player when quest is accepted");

			if (sourceItemId != 0)
			{
				ImGui::Indent();
				ImGui::SetNextItemWidth(100);
				SLIDER_UINT32_PROP(srcitemcount, "Item Count", 1, 255);
				ImGui::Unindent();
			}

			// Ensure we don't have ourself as a prerequisite
			uint32 prevQuestId = currentEntry.prevquestid();
			if (prevQuestId == currentEntry.id())
			{
				currentEntry.set_prevquestid(0);
				prevQuestId = 0;
			}

			const auto* questEntry = m_project.quests.getById(prevQuestId);
			ImGui::SetNextItemWidth(300);
			if (ImGui::BeginCombo("##PrevQuest", questEntry != nullptr ? questEntry->name().c_str() : "(None)", ImGuiComboFlags_None))
			{
				ImGui::PushID(0);
				if (ImGui::Selectable("(None)"))
				{
					currentEntry.set_prevquestid(0);
					prevQuestId = 0;
				}
				ImGui::PopID();

				for (int i = 0; i < m_project.quests.count(); i++)
				{
					if (m_project.quests.getTemplates().entry(i).id() == currentEntry.id())
					{
						continue;
					}

					ImGui::PushID(i);
					const bool item_selected = m_project.quests.getTemplates().entry(i).id() == prevQuestId;
					const char* item_text = m_project.quests.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_prevquestid(m_project.quests.getTemplates().entry(i).id());
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
			ImGui::Text("Required Previous Quest");
			ImGui::SameLine();
			DrawHelpMarker("Quest that must be completed before this one");

			ImGui::Spacing();
			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(suggestedplayers, "Suggested Players", 0, 40);
			ImGui::SameLine();
			DrawHelpMarker("Recommended number of players (0 = solo quest)");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Classes & Races"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
			ImGui::TextWrapped("If none are checked, all races and classes are allowed to accept this quest.");
			ImGui::PopStyleColor();
			ImGui::Spacing();

			DrawSectionHeader("Required Races");

			if (ImGui::BeginTable("requiredRaces", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
			{
				for (uint32 i = 0; i < 32; ++i)
				{
					if (proto::RaceEntry* race = m_project.races.getById(i))
					{
						ImGui::TableNextColumn();

						bool raceIncluded = (currentEntry.requiredraces() & (1 << (i - 1))) != 0;
						if (ImGui::Checkbox(race->name().c_str(), &raceIncluded))
						{
							if (raceIncluded)
								currentEntry.set_requiredraces(currentEntry.requiredraces() | (1 << (i - 1)));
							else
								currentEntry.set_requiredraces(currentEntry.requiredraces() & ~(1 << (i - 1)));
						}
					}
				}

				ImGui::EndTable();
			}

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Required Classes");

			if (ImGui::BeginTable("requiredClasses", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
			{
				for (uint32 i = 0; i < 32; ++i)
				{
					if (proto::ClassEntry* classEntry = m_project.classes.getById(i))
					{
						ImGui::TableNextColumn();

						bool classIncluded = (currentEntry.requiredclasses() & (1 << (i - 1))) != 0;
						if (ImGui::Checkbox(classEntry->name().c_str(), &classIncluded))
						{
							if (classIncluded)
								currentEntry.set_requiredclasses(currentEntry.requiredclasses() | (1 << (i - 1)));
							else
								currentEntry.set_requiredclasses(currentEntry.requiredclasses() & ~(1 << (i - 1)));
						}
					}
				}

				ImGui::EndTable();
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Quest Text"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Quest Details");
			ImGui::Text("Details Text");
			ImGui::SameLine();
			DrawHelpMarker("Shown when player is offered the quest");
			ImGui::InputTextMultiline("##Details", currentEntry.mutable_detailstext(), ImVec2(-1, 80));

			ImGui::Spacing();
			ImGui::Text("Objectives Text");
			ImGui::SameLine();
			DrawHelpMarker("Brief description of quest objectives");
			ImGui::InputTextMultiline("##Objectives", currentEntry.mutable_objectivestext(), ImVec2(-1, 60));

			ImGui::Spacing();
			ImGui::Text("Offer Reward Text");
			ImGui::SameLine();
			DrawHelpMarker("Shown when quest is ready to be turned in");
			ImGui::InputTextMultiline("##OfferReward", currentEntry.mutable_offerrewardtext(), ImVec2(-1, 80));

			ImGui::Spacing();
			ImGui::Text("Request Items Text");
			ImGui::SameLine();
			DrawHelpMarker("Shown if quest requires items to be turned in");
			ImGui::InputTextMultiline("##RequestItems", currentEntry.mutable_requestitemstext(), ImVec2(-1, 60));

			ImGui::Spacing();
			ImGui::Text("End Text");
			ImGui::SameLine();
			DrawHelpMarker("Shown when quest is completed");
			ImGui::InputTextMultiline("##End", currentEntry.mutable_endtext(), ImVec2(-1, 60));

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Objectives"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Quest Objectives");

			ImGui::BeginDisabled(currentEntry.requirements_size() >= 4);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("+ Add Objective", ImVec2(150, 30)))
			{
				auto* newEntry = currentEntry.add_requirements();
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();
			
			ImGui::SameLine();
			if (currentEntry.requirements_size() >= 4)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
				ImGui::TextWrapped("Maximum of 4 objectives reached");
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextDisabled("%d / 4 objectives", currentEntry.requirements_size());
			}

			ImGui::Spacing();

			if (currentEntry.requirements_size() > 0 && ImGui::BeginTable("questRequirements", 6, 
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | 
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80);
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 80);
				ImGui::TableSetupColumn("Creature", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Custom Text", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.requirements_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_requirements(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					// Type indicator
					ImGui::TableNextColumn();
					if (currentItem->itemid() != 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
						ImGui::Text("Item");
						ImGui::PopStyleColor();
					}
					else if (currentItem->creatureid() != 0)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
						ImGui::Text("Kill");
						ImGui::PopStyleColor();
					}
					else
					{
						ImGui::TextDisabled("None");
					}

					// Item selection
					ImGui::TableNextColumn();
					uint32 item = currentItem->itemid();
					const auto* itemEntry = m_project.items.getById(item);
					if (ImGui::BeginCombo("##item", itemEntry != nullptr ? itemEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						if (ImGui::Selectable("None"))
						{
							currentItem->set_itemid(0);
						}
						else
						{
							for (int i = 0; i < m_project.items.count(); i++)
							{
								ImGui::PushID(i);
								const bool item_selected = m_project.items.getTemplates().entry(i).id() == item;
								const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
								if (ImGui::Selectable(item_text, item_selected))
								{
									currentItem->set_itemid(m_project.items.getTemplates().entry(i).id());
									if (currentItem->itemcount() == 0)
									{
										currentItem->set_itemcount(1);
									}

									// Reset other requirements back to 0
									currentItem->set_creatureid(0);
									currentItem->set_objectid(0);
									currentItem->set_spellcast(0);
								}
								if (item_selected)
								{
									ImGui::SetItemDefaultFocus();
								}
								ImGui::PopID();
							}
						}
						
						ImGui::EndCombo();
					}

					// Item/Creature count
					ImGui::TableNextColumn();
					if (currentItem->itemid() != 0)
					{
						// Item count input
						int32 count = currentItem->itemcount();
						ImGui::SetNextItemWidth(-1);
						if (ImGui::InputInt("##item_count", &count))
						{
							count = Clamp(count, 1, 255);
							currentItem->set_itemcount(count);
						}
					}
					else if (currentItem->creatureid() != 0)
					{
						// Creature count input
						int32 count = currentItem->creaturecount();
						ImGui::SetNextItemWidth(-1);
						if (ImGui::InputInt("##creature_count", &count))
						{
							count = Clamp(count, 1, 255);
							currentItem->set_creaturecount(count);
						}
					}

					// Creature selection
					ImGui::TableNextColumn();
					uint32 creature = currentItem->creatureid();
					const auto* creatureEntry = m_project.units.getById(creature);
					if (ImGui::BeginCombo("##creature", creatureEntry != nullptr ? creatureEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						if (ImGui::Selectable("None"))
						{
							currentItem->set_creatureid(0);
						}
						else
						{
							for (int i = 0; i < m_project.units.count(); i++)
							{
								ImGui::PushID(i);
								const bool item_selected = m_project.units.getTemplates().entry(i).id() == creature;
								const char* item_text = m_project.units.getTemplates().entry(i).name().c_str();
								if (ImGui::Selectable(item_text, item_selected))
								{
									currentItem->set_creatureid(m_project.units.getTemplates().entry(i).id());
									if (currentItem->creaturecount() == 0)
									{
										currentItem->set_creaturecount(1);
									}

									// Reset other requirements back to 0
									currentItem->set_itemid(0);
									currentItem->set_objectid(0);
									currentItem->set_spellcast(0);
								}
								if (item_selected)
								{
									ImGui::SetItemDefaultFocus();
								}
								ImGui::PopID();
							}
						}

						ImGui::EndCombo();
					}

					// Custom text
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-1);
					ImGui::InputText("##custom_text", currentItem->mutable_text());

					// Actions
					ImGui::TableNextColumn();
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
					if (ImGui::Button("Remove", ImVec2(-1, 0)))
					{
						currentEntry.mutable_requirements()->erase(currentEntry.mutable_requirements()->begin() + index);
						index--;
					}
					ImGui::PopStyleColor();

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			else if (currentEntry.requirements_size() == 0)
			{
				ImGui::TextDisabled("No objectives defined yet. Click 'Add Objective' to create one.");
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Rewards"))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Experience & Money");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(rewardxp, "Experience Points", 0, std::numeric_limits<int32>::max());
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.8f, 0.8f));
			if (ImGui::Button("Auto-Calculate XP"))
			{
				float difficultyMultiplier = 1.0f;

				if (currentEntry.requirements_size() == 0 && (currentEntry.flags() & quest_flags::Exploration) == 0)
				{
					difficultyMultiplier = 0.25f;
				}
				else
				{
					if (currentEntry.suggestedplayers() >= 5)
					{
						difficultyMultiplier = 2.0f;
					}
					else if (currentEntry.suggestedplayers() >= 3)
					{
						difficultyMultiplier = 1.5f;
					}
					else if (currentEntry.suggestedplayers() >= 2)
					{
						difficultyMultiplier = 1.25f;
					}
				}

				currentEntry.set_rewardxp(GetSuggestedQuestXP(currentEntry.questlevel(), difficultyMultiplier));
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();
			DrawHelpMarker("Automatically calculate XP based on quest level and difficulty");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(rewardmoney, "Money Reward", 0, std::numeric_limits<int32>::max());
			ImGui::SameLine();
			DrawHelpMarker("Copper coins rewarded");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Item Rewards");

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("+ Add Reward Item", ImVec2(150, 30)))
			{
				auto* newEntry = currentEntry.add_rewarditems();
			}
			ImGui::PopStyleColor();
			
			ImGui::SameLine();
			ImGui::TextDisabled("%d reward items", currentEntry.rewarditems_size());

			ImGui::Spacing();

			if (currentEntry.rewarditems_size() > 0 && ImGui::BeginTable("rewardItems", 4, 
				ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | 
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30);
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 100);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.rewarditems_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_rewarditems(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					ImGui::Text("%d", index + 1);

					ImGui::TableNextColumn();
					uint32 item = currentItem->itemid();
					const auto* itemEntry = m_project.items.getById(item);
					if (ImGui::BeginCombo("##rewardItem", itemEntry != nullptr ? itemEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						if (ImGui::Selectable("None"))
						{
							currentItem->set_itemid(0);
						}
						else
						{
							for (int i = 0; i < m_project.items.count(); i++)
							{
								ImGui::PushID(i);
								const bool item_selected = m_project.items.getTemplates().entry(i).id() == item;
								const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
								if (ImGui::Selectable(item_text, item_selected))
								{
									currentItem->set_itemid(m_project.items.getTemplates().entry(i).id());
									if (currentItem->count() == 0)
									{
										currentItem->set_count(1);
									}
								}
								if (item_selected)
								{
									ImGui::SetItemDefaultFocus();
								}
								ImGui::PopID();
							}
						}

						ImGui::EndCombo();
					}

					ImGui::TableNextColumn();
					int32 count = currentItem->count();
					ImGui::SetNextItemWidth(-1);
					if (ImGui::InputInt("##reward_item_count", &count))
					{
						count = Clamp(count, 1, 255);
						currentItem->set_count(count);
					}

					ImGui::TableNextColumn();
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
					if (ImGui::Button("Remove", ImVec2(-1, 0)))
					{
						currentEntry.mutable_rewarditems()->erase(currentEntry.mutable_rewarditems()->begin() + index);
						index--;
					}
					ImGui::PopStyleColor();

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			else if (currentEntry.rewarditems_size() == 0)
			{
				ImGui::TextDisabled("No item rewards defined. Click 'Add Reward Item' to add one.");
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}
	}

	void QuestEditorWindow::OnNewEntry(proto::TemplateManager<proto::Quests, proto::QuestEntry>::EntryType& entry)
	{
		EditorEntryWindowBase<proto::Quests, proto::QuestEntry>::OnNewEntry(entry);

		entry.set_type(2);
	}
}
