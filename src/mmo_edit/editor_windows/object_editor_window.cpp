// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "object_editor_window.h"
#include "editor_imgui_helpers.h"
#include "loot_assignment_widget.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/object_type_id.h"
#include "game/world_object_flags.h"
#include "log/default_log_levels.h"

namespace mmo
{
	ObjectEditorWindow::ObjectEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.objects, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Objects";
	}

	void ObjectEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (ImGui::Button("Duplicate"))
		{
			proto::ObjectEntry* copied = m_project.objects.add();
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
#define SLIDER_UINT32_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 32, min, max)
#define SLIDER_UINT64_PROP(name, label, min, max) SLIDER_UNSIGNED_PROP(name, label, 64, min, max)

		if (const auto section = ScopedEditorSection("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 4, ImGuiTableFlags_None))
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

			// Object type dropdown
			static const char* s_objectTypeNames[] = { "Chest", "Door", "Mailbox", "Quest Object" };
			static_assert(std::size(s_objectTypeNames) == game_world_object_type::Count_,
				"s_objectTypeNames must match game_world_object_type::Type");

			int objectType = static_cast<int>(currentEntry.type());
			if (objectType < 0 || objectType >= game_world_object_type::Count_)
				objectType = 0;

			if (ImGui::BeginCombo("Type", s_objectTypeNames[objectType]))
			{
				for (int i = 0; i < game_world_object_type::Count_; ++i)
				{
					const bool selected = (objectType == i);
					if (ImGui::Selectable(s_objectTypeNames[i], selected))
					{
						currentEntry.set_type(static_cast<uint32>(i));
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			CHECKBOX_FLAG_PROP(flags, "Not Interactable (trigger-only)", world_object_flags::NotInteractable);
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Players can never use this object; it only reacts to triggers\n(e.g. boss doors opened via SetWorldObjectState).");

			// Migrate the legacy single loot table assignment into the new loot table list so existing
			// objects keep their loot as a one-element list.
			if (currentEntry.objectlootentries_size() == 0 && currentEntry.has_objectlootentry())
			{
				if (m_project.unitLoot.getById(currentEntry.objectlootentry()) != nullptr)
				{
					currentEntry.add_objectlootentries(currentEntry.objectlootentry());
				}
				currentEntry.clear_objectlootentry();
			}

			DrawLootTableAssignment(m_project, *currentEntry.mutable_objectlootentries());
		}

		static const char* s_noneEntryString = "<None>";

		if (const auto section = ScopedEditorSection("Lock", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const uint32 objType = currentEntry.type();
			const bool isChest = (objType == game_world_object_type::Chest);
			const bool isDoor  = (objType == game_world_object_type::Door);

			if (!isChest && !isDoor)
			{
				ImGui::TextDisabled("No lock properties for this object type.");
			}
			else
			{
				// Ensure data[] has enough slots: data[0]/data[1] for locks, data[2] for the
				// door auto close time.
				while (currentEntry.data_size() < (isDoor ? 3 : 2))
					currentEntry.add_data(0);

				// data[0] — default (active) lock type
				{
					const uint32 lockTypeId = currentEntry.data(0);
					const auto* lockEntry = m_project.lockTypes.getById(lockTypeId);
					const char* lockPreview = (lockEntry != nullptr) ? lockEntry->name().c_str() : "None (always open)";

					if (ImGui::BeginCombo("Lock Type##data0", lockPreview))
					{
						ImGui::PushID(0);
						if (ImGui::Selectable("None (always open)", lockTypeId == 0))
							currentEntry.mutable_data()->Set(0, 0);
						if (lockTypeId == 0) ImGui::SetItemDefaultFocus();
						ImGui::PopID();

						for (int i = 0; i < m_project.lockTypes.count(); ++i)
						{
							ImGui::PushID(i + 1);
							const uint32 entryId = m_project.lockTypes.getTemplates().entry(i).id();
							const bool selected = (entryId == lockTypeId);
							const char* text = m_project.lockTypes.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(text, selected))
								currentEntry.mutable_data()->Set(0, entryId);
							if (selected) ImGui::SetItemDefaultFocus();
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					ImGui::TextDisabled("(?)");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("The lock type required to open this object.\nSet to None to make it always accessible.");
				}

				// data[1] — post-unlock lock type (Door only)
				if (isDoor)
				{
					const uint32 postUnlockId = currentEntry.data(1);
					const auto* postEntry = m_project.lockTypes.getById(postUnlockId);
					const char* postPreview = (postEntry != nullptr) ? postEntry->name().c_str() : "None (no change)";

					if (ImGui::BeginCombo("Post-Unlock Lock Type##data1", postPreview))
					{
						ImGui::PushID(0);
						if (ImGui::Selectable("None (no change)", postUnlockId == 0))
							currentEntry.mutable_data()->Set(1, 0);
						if (postUnlockId == 0) ImGui::SetItemDefaultFocus();
						ImGui::PopID();

						for (int i = 0; i < m_project.lockTypes.count(); ++i)
						{
							ImGui::PushID(i + 1);
							const uint32 entryId = m_project.lockTypes.getTemplates().entry(i).id();
							const bool selected = (entryId == postUnlockId);
							const char* text = m_project.lockTypes.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(text, selected))
								currentEntry.mutable_data()->Set(1, entryId);
							if (selected) ImGui::SetItemDefaultFocus();
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					ImGui::TextDisabled("(?)");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("After the first successful unlock, the door's active lock type\nchanges to this value. Set to None to keep the original lock type.");

					// data[2] — auto close time in milliseconds
					{
						uint32 autoCloseMs = currentEntry.data(2);
						if (ImGui::InputScalar("Auto Close Time (ms)##data2", ImGuiDataType_U32, &autoCloseMs, nullptr, nullptr))
						{
							currentEntry.mutable_data()->Set(2, autoCloseMs);
						}
						ImGui::SameLine();
						ImGui::TextDisabled("(?)");
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("Time in milliseconds after opening before the door closes itself.\n0 = stays open until closed by a player or trigger.");
					}
				}
			}
		}

		if (const auto section = ScopedEditorSection("Triggers", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Triggers listening to OnInteraction fire whenever a player successfully uses this object.");

			// Display existing triggers in a table
			if (currentEntry.triggers_size() > 0 && ImGui::BeginTable("ObjectTriggerTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
				ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableHeadersRow();

				for (int i = 0; i < currentEntry.triggers_size(); ++i)
				{
					const uint32 triggerId = currentEntry.triggers(i);

					ImGui::TableNextRow();
					ImGui::PushID(i);

					// ID Column
					ImGui::TableNextColumn();
					ImGui::Text("%u", triggerId);

					// Name Column
					ImGui::TableNextColumn();
					const auto* trigger = m_project.triggers.getById(triggerId);
					ImGui::TextUnformatted(trigger ? trigger->name().c_str() : "(Unknown Trigger)");

					// Actions Column
					ImGui::TableNextColumn();
					if (DrawDangerButton("Remove"))
					{
						auto* triggers = currentEntry.mutable_triggers();
						triggers->erase(triggers->begin() + i);
						i--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			// Add trigger button
			if (DrawSuccessButton("Add Trigger"))
			{
				ImGui::OpenPopup("AddObjectTriggerPopup");
			}

			// Add Trigger Popup
			if (ImGui::BeginPopupModal("AddObjectTriggerPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				// Static filter to allow user to search triggers by name or ID.
				static ImGuiTextFilter triggerFilter;
				triggerFilter.Draw("Filter (by name or ID)", 200.0f);

				ImGui::Text("Select a trigger to add:");
				ImGui::Separator();

				ImGui::BeginChild("ObjectTriggersList", ImVec2(400, 300), true);

				static int selectedTriggerId = -1;

				for (int i = 0; i < m_project.triggers.count(); ++i)
				{
					const auto& trigger = m_project.triggers.getTemplates().entry(i);
					char label[128];
					snprintf(label, sizeof(label), "%u - %s", trigger.id(), trigger.name().c_str());

					if (!triggerFilter.PassFilter(label))
						continue;

					if (ImGui::Selectable(label, selectedTriggerId == trigger.id()))
					{
						selectedTriggerId = trigger.id();
					}
				}

				ImGui::EndChild();

				ImGui::Separator();

				if (DrawSuccessButton("OK", ImVec2(120, 0)))
				{
					if (selectedTriggerId != -1)
					{
						currentEntry.add_triggers(selectedTriggerId);
					}
					selectedTriggerId = -1;
					triggerFilter.Clear();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (DrawNeutralButton("Cancel", ImVec2(120, 0)))
				{
					selectedTriggerId = -1;
					triggerFilter.Clear();
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}
		}

		if (const auto section = ScopedEditorSection("Factions", ImGuiTreeNodeFlags_None))
		{
			int32 factionTemplate = currentEntry.factionid();

			const auto* factionEntry = m_project.factionTemplates.getById(factionTemplate);
			if (ImGui::BeginCombo("Faction Template", factionEntry != nullptr ? factionEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.factionTemplates.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.factionTemplates.getTemplates().entry(i).id() == factionTemplate;
					const char* item_text = m_project.factionTemplates.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_factionid(m_project.factionTemplates.getTemplates().entry(i).id());
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

		if (const auto section = ScopedEditorSection("Quests", ImGuiTreeNodeFlags_None))
		{
			// Required Quest for Usability
			ImGui::Separator();
			ImGui::Text("Usability");

			uint32 requiredQuestId = currentEntry.has_requiredquest() ? currentEntry.requiredquest() : 0;
			const auto* requiredQuestEntry = requiredQuestId != 0 ? m_project.quests.getById(requiredQuestId) : nullptr;

			if (ImGui::BeginCombo("Required Quest", requiredQuestEntry != nullptr ? requiredQuestEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				// None option
				ImGui::PushID(-1);
				if (ImGui::Selectable(s_noneEntryString, requiredQuestId == 0))
				{
					currentEntry.set_requiredquest(0);
				}
				ImGui::PopID();

				for (int i = 0; i < m_project.quests.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.quests.getTemplates().entry(i).id() == requiredQuestId;
					const char* item_text = m_project.quests.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_requiredquest(m_project.quests.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (requiredQuestId != 0)
			{
				ImGui::TextWrapped("This object can only be used by players who have this quest active.");
			}

			ImGui::Separator();
			ImGui::Text("Quest Interactions");

			ImGui::Text("Offers Quests");

			// Add button
			if (ImGui::Button("Add Offered Quest", ImVec2(-1, 0)))
			{
				currentEntry.add_quests(0);
			}

			if (ImGui::BeginTable("offeredQuests", 1, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableHeadersRow();

				int index = 0;
				for (auto& quest : currentEntry.quests())
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImGui::PushID(index);

					const auto* questEntry = m_project.quests.getById(quest);
					if (ImGui::BeginCombo("##quest", questEntry != nullptr ? questEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.quests.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.quests.getTemplates().entry(i).id() == quest;
							const char* item_text = m_project.quests.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.mutable_quests()->Set(index, m_project.quests.getTemplates().entry(i).id());
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
						currentEntry.mutable_quests()->erase(currentEntry.mutable_quests()->begin() + index);
						index--;
					}

					ImGui::PopID();
					index++;
				}

				ImGui::EndTable();
			}

			ImGui::Text("Completes Quests");

			// Add button
			if (ImGui::Button("Add Completed Quest", ImVec2(-1, 0)))
			{
				currentEntry.add_end_quests(0);
			}

			if (ImGui::BeginTable("endedQuests", 1, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableHeadersRow();

				int index = 0;
				for (auto& quest : currentEntry.end_quests())
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImGui::PushID(index);

					const auto* questEntry = m_project.quests.getById(quest);
					if (ImGui::BeginCombo("##end_quests", questEntry != nullptr ? questEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.quests.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.quests.getTemplates().entry(i).id() == quest;
							const char* item_text = m_project.quests.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.mutable_end_quests()->Set(index, m_project.quests.getTemplates().entry(i).id());
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
						currentEntry.mutable_end_quests()->erase(currentEntry.mutable_end_quests()->begin() + index);
						index--;
					}

					ImGui::PopID();
					index++;
				}

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Visuals", ImGuiTreeNodeFlags_None))
		{
			int32 displayId = currentEntry.displayid();

			const auto* displayIdEntry = m_project.objectDisplays.getById(displayId);
			if (ImGui::BeginCombo("Model", displayIdEntry != nullptr ? displayIdEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.objectDisplays.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.objectDisplays.getTemplates().entry(i).id() == displayId;
					const char* item_text = m_project.objectDisplays.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_displayid(m_project.objectDisplays.getTemplates().entry(i).id());
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
	}

	void ObjectEditorWindow::OnNewEntry(proto::TemplateManager<proto::Objects, proto::ObjectEntry>::EntryType& entry)
	{
		entry.set_factionid(0);
		entry.set_displayid(0);
		entry.set_type(0);
	}
}
