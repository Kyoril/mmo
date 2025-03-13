// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "quest_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"
#include "math/clamp.h"

namespace mmo
{
	QuestEditorWindow::QuestEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.quests, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Quests";
	}

	void QuestEditorWindow::DrawDetailsImpl(proto::QuestEntry& currentEntry)
	{
		if (ImGui::Button("Duplicate Quest"))
		{
			proto::QuestEntry* copied = m_project.quests.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}

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

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 3, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Internal Name", currentEntry.mutable_internalname());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Quest Title", currentEntry.mutable_name());
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

			SLIDER_UINT32_PROP(questlevel, "Quest Level", 0, 255);
			SLIDER_UINT32_PROP(minlevel, "Min Level", 0, 255);
			SLIDER_UINT32_PROP(maxlevel, "Max Level", 0, 255);

			ImGui::Text("Quest Type");
			ImGui::SameLine();
			if (ImGui::RadioButton("Turn In", currentEntry.type() == 0))
			{
				currentEntry.set_type(0);
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Task", currentEntry.type() == 1))
			{
				currentEntry.set_type(1);
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Quest", currentEntry.type() == 2))
			{
				currentEntry.set_type(2);
			}

			uint32 sourceItemId = currentEntry.srcitemid();

			const auto* itemEntry = m_project.items.getById(sourceItemId);
			if (ImGui::BeginCombo("Initial Quest Item", itemEntry != nullptr ? itemEntry->name().c_str() : "None", ImGuiComboFlags_None))
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

			if (sourceItemId != 0)
			{
				SLIDER_UINT32_PROP(srcitemcount, "Number of source item to give", 1, 255);
			}

			// Ensure we don't have ourself as a prerequisite
			uint32 prevQuestId = currentEntry.prevquestid();
			if (prevQuestId == currentEntry.id())
			{
				currentEntry.set_prevquestid(0);
				prevQuestId = 0;
			}

			const auto* questEntry = m_project.quests.getById(prevQuestId);
			if (ImGui::BeginCombo("Previous Quest", questEntry != nullptr ? questEntry->name().c_str() : "(None)", ImGuiComboFlags_None))
			{
				ImGui::PushID(0);
				if (ImGui::Selectable("(None)"))
				{
					currentEntry.set_prevquestid(0);
					sourceItemId = 0;
				}
				ImGui::PopID();

				for (int i = 0; i < m_project.quests.count(); i++)
				{
					if (m_project.quests.getTemplates().entry(i).id() == currentEntry.id())
					{
						continue;
					}

					ImGui::PushID(i);
					const bool item_selected = m_project.quests.getTemplates().entry(i).id() == sourceItemId;
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

		}

		if (ImGui::CollapsingHeader("Classes / Races", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text("If none are checked, all races / classes are allowed.");

			ImGui::Text("Required Races");

			if (ImGui::BeginTable("requiredRaces", 4, ImGuiTableFlags_None))
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

			ImGui::Text("Required Classes");

			if (ImGui::BeginTable("requiredClasses", 4, ImGuiTableFlags_None))
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
		}

		if (ImGui::CollapsingHeader("Quest Text", ImGuiTreeNodeFlags_None))
		{
			ImGui::InputTextMultiline("Details", currentEntry.mutable_detailstext());
			ImGui::InputTextMultiline("Objectives", currentEntry.mutable_objectivestext());
			ImGui::InputTextMultiline("Offer Reward", currentEntry.mutable_offerrewardtext());
			ImGui::InputTextMultiline("Request Items", currentEntry.mutable_requestitemstext());
			ImGui::InputTextMultiline("End", currentEntry.mutable_endtext());
		}

		if (ImGui::CollapsingHeader("Completion Criteria", ImGuiTreeNodeFlags_None))
		{
			// Add button
			ImGui::BeginDisabled(currentEntry.requirements_size() >= 4);
			if (ImGui::Button("Add", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_requirements();
			}
			if (currentEntry.requirements_size() >= 4)
			{
				ImGui::Text("Up to 4 requirements per quest with a counter of up to 255 elements each are currently allowed due to the way we sync quest progress to the client.");
			}
			ImGui::EndDisabled();

			if (ImGui::BeginTable("questRequirements", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Item Count", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Creature", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Creature Count", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Custom Text", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.requirements_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_requirements(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

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

					ImGui::TableNextColumn();

					ImGui::BeginDisabled(currentItem->itemid() == 0);
					int32 count = currentItem->itemcount();
					if (ImGui::InputInt("##item_count", &count))
					{
						count = Clamp(count, 1, 255);
						currentItem->set_itemcount(count);
					}
					ImGui::EndDisabled();

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

					ImGui::TableNextColumn();

					ImGui::BeginDisabled(currentItem->creatureid() == 0);
					count = currentItem->creaturecount();
					if (ImGui::InputInt("##creature_count", &count))
					{
						count = Clamp(count, 1, 255);
						currentItem->set_creaturecount(count);
					}
					ImGui::EndDisabled();

					ImGui::TableNextColumn();

					ImGui::InputText("##custom_text", currentItem->mutable_text());

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_requirements()->erase(currentEntry.mutable_requirements()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Rewards", ImGuiTreeNodeFlags_None))
		{
			SLIDER_UINT32_PROP(rewardxp, "Rewarded Xp", 0, std::numeric_limits<int32>::max());
			SLIDER_UINT32_PROP(rewardmoney, "Rewarded Money", 0, std::numeric_limits<int32>::max());

			// Add button
			if (ImGui::Button("Add", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_rewarditems();
			}

			if (ImGui::BeginTable("rewardItems", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Item Count", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.rewarditems_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_rewarditems(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

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
					if (ImGui::InputInt("##reward_item_count", &count))
					{
						count = Clamp(count, 1, 255);
						currentItem->set_count(count);
					}

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_rewarditems()->erase(currentEntry.mutable_rewarditems()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
	}

	void QuestEditorWindow::OnNewEntry(proto::TemplateManager<proto::Quests, proto::QuestEntry>::EntryType& entry)
	{
		EditorEntryWindowBase<proto::Quests, proto::QuestEntry>::OnNewEntry(entry);

		entry.set_type(2);
	}
}
