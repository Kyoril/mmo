// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "gossip_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/gossip.h"
#include "log/default_log_levels.h"
#include "math/clamp.h"

namespace mmo
{
	GossipEditorWindow::GossipEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.gossipMenus, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Gossip";
	}

	void GossipEditorWindow::DrawDetailsImpl(proto::GossipMenuEntry& currentEntry)
	{
		if (ImGui::Button("Duplicate Gossip Menu"))
		{
			proto::GossipMenuEntry* copied = m_project.gossipMenus.add();
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

			// If this property is set, quests will be shown in this gossip menu to accept / turn in
			CHECKBOX_BOOL_PROP(show_quests, "Show Quest Menu for this page");

			ImGui::InputTextMultiline("Greeting Text", currentEntry.mutable_text());

			// Draw condition selection
			int conditionid = currentEntry.conditionid();
			const auto* condition = m_project.conditions.getById(conditionid);
			if (ImGui::BeginCombo("Menu Condition", condition ? condition->name().c_str() : "<None>", ImGuiComboFlags_None))
			{
				ImGui::PushID(-1);
				if (ImGui::Selectable("<None>", conditionid == 0))
				{
					currentEntry.set_conditionid(0);
				}
				if (conditionid == 0)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();

				for (int i = 0; i < m_project.conditions.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.conditions.getTemplates().entry(i).id() == currentEntry.conditionid();
					const char* item_text = m_project.conditions.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_conditionid(m_project.conditions.getTemplates().entry(i).id());
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

		if (ImGui::CollapsingHeader("Gossip Options", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::Button("Add Option"))
			{
				// Ensure each existing option has it's index + 1 as new id first
				for (int i = 0; i < currentEntry.options_size(); ++i)
				{
					auto* option = currentEntry.mutable_options(i);
					option->set_id(i + 1);
				}

				mmo::proto::GossipMenuOption* option = currentEntry.add_options();
				option->set_id(currentEntry.options_size());
				option->set_action_type(gossip_actions::None);
				option->clear_action_param();
				option->set_text("TODO");
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove All Actions"))
			{
				currentEntry.clear_options();
			}

			static const char* s_spellNone = "<None>";

			if (ImGui::BeginTable("optionsTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Action Type", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.options_size(); ++index)
				{
					auto* mutableEntry = currentEntry.mutable_options(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					ImGui::InputText("##text", mutableEntry->mutable_text());
					ImGui::TableNextColumn();

					static const char* s_actionTypeStrings[] = { "None", "Show Vendor Menu", "Show Trainer Menu", "Show Gossip Menu" };
					static_assert(std::size(s_actionTypeStrings) == gossip_actions::Count_, "Action type strings must match gossip_actions enum");

					int actionType = mutableEntry->action_type();
					if (ImGui::Combo("##actionType", &actionType,
						[](void* data, int idx, const char** out_text)
						{
							if (idx < 0 || idx >= IM_ARRAYSIZE(s_actionTypeStrings))
							{
								return false;
							}

							*out_text = s_actionTypeStrings[idx];
							return true;
						}, nullptr, IM_ARRAYSIZE(s_actionTypeStrings)))
					{
						mutableEntry->set_action_type(actionType);
					}
					ImGui::TableNextColumn();

					switch (actionType)
					{
					case gossip_actions::None:
					case gossip_actions::Vendor:
					case gossip_actions::Trainer:
						// Nothing to show for these gossip actions as there is no parameter
						if (mutableEntry->has_action_param())
						{
							mutableEntry->clear_action_param();
						}
						break;
					case gossip_actions::GossipMenu:
						{
						const auto* gossipEntry = m_project.gossipMenus.getById(mutableEntry->action_param());
						if (ImGui::BeginCombo("##gossipMenuParam", gossipEntry != nullptr ? gossipEntry->name().c_str() : "<None>", ImGuiComboFlags_None))
						{
							for (int i = 0; i < m_project.gossipMenus.count(); i++)
							{
								if (m_project.gossipMenus.getTemplates().entry(i).id() == currentEntry.id())
								{
									// Do not allow linking to the same gossip menu
									continue;
								}

								ImGui::PushID(i);
								const bool item_selected = m_project.gossipMenus.getTemplates().entry(i).id() == mutableEntry->conditionid();
								const char* item_text = m_project.gossipMenus.getTemplates().entry(i).name().c_str();
								if (ImGui::Selectable(item_text, item_selected))
								{
									mutableEntry->set_action_param(m_project.gossipMenus.getTemplates().entry(i).id());
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
						break;
					}

					ImGui::TableNextColumn();

					// Draw condition selection
					int conditionid = mutableEntry->conditionid();
					const auto* condition = m_project.conditions.getById(conditionid);

					if (ImGui::BeginCombo("##actionCondition", condition ? condition->name().c_str() : "<None>", ImGuiComboFlags_None))
					{
						ImGui::PushID(-1);
						if (ImGui::Selectable("<None>", conditionid == 0))
						{
							mutableEntry->set_conditionid(0);
						}
						if (conditionid == 0)
						{
							ImGui::SetItemDefaultFocus();
						}
						ImGui::PopID();


						for (int i = 0; i < m_project.conditions.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.conditions.getTemplates().entry(i).id() == mutableEntry->action_param();
							const char* item_text = m_project.conditions.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								mutableEntry->set_conditionid(m_project.conditions.getTemplates().entry(i).id());
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
						currentEntry.mutable_options()->DeleteSubrange(index, 1);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}
	}

	void GossipEditorWindow::OnNewEntry(proto::TemplateManager<proto::GossipMenus, proto::GossipMenuEntry>::EntryType& entry)
	{
		EditorEntryWindowBase<proto::GossipMenus, proto::GossipMenuEntry>::OnNewEntry(entry);

		entry.set_text("Greetings, $N!");
		entry.set_show_quests(true);
	}
}
