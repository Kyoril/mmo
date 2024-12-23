// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_loot_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game_server/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	UnitLootEditorWindow::UnitLootEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.unitLoot, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Unit Loot";
	}

	void UnitLootEditorWindow::DrawDetailsImpl(proto::LootEntry& currentEntry)
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

#define MONEY_PROP_LABEL(name) \
	{ \
		const int32 gold = ::floor(currentEntry.name() / 10000); \
		const int32 silver = ::floor(::fmod(currentEntry.name(), 10000) / 100);\
		const int32 copper = ::fmod(currentEntry.name(), 100);\
		if (gold > 0) \
		{ \
			ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.0f, 1.0f), "%d g", gold); \
			ImGui::SameLine(); \
		} \
		if (silver > 0 || gold > 0) \
		{ \
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%d s", silver); \
			ImGui::SameLine(); \
		} \
		ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.0f, 1.0f), "%d c", copper); \
	}

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
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
		}

		if (ImGui::CollapsingHeader("Money", ImGuiTreeNodeFlags_None))
		{
			SLIDER_UINT32_PROP(minmoney, "Min Money", 0, 1000000);
			ImGui::SameLine();
			MONEY_PROP_LABEL(minmoney);

			SLIDER_UINT32_PROP(maxmoney, "Max Money", 0, 1000000);
			ImGui::SameLine();
			MONEY_PROP_LABEL(maxmoney);

		}

		if (ImGui::CollapsingHeader("Groups", ImGuiTreeNodeFlags_None))
		{
			if (ImGui::Button("Add Group"))
			{
				auto* group = currentEntry.add_groups();
			}

			static const char* s_spellNone = "<None>";

			for (int groupId = 0; groupId < currentEntry.groups_size(); ++groupId)
			{
				auto& group = *currentEntry.mutable_groups(groupId);

				ImGui::PushID(groupId);

				if (ImGui::CollapsingHeader(("Group " + std::to_string(groupId)).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (ImGui::Button("Add Item"))
					{
						auto* definition = group.add_definitions();
					}

					if (ImGui::BeginTable("groupItems", 6, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
					{
						ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Chance", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Min Count", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Max Count", ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed);
						ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed);
						ImGui::TableHeadersRow();

						for (int index = 0; index < group.definitions_size(); ++index)
						{
							auto* definition = group.mutable_definitions(index);

							ImGui::PushID(index);
							ImGui::TableNextRow();

							ImGui::TableNextColumn();

							// Item
							uint32 item = definition->item();

							const auto* itemEntry = m_project.items.getById(item);
							if (ImGui::BeginCombo("##item", itemEntry != nullptr ? itemEntry->name().c_str() : s_spellNone, ImGuiComboFlags_None))
							{
								for (int i = 0; i < m_project.items.count(); i++)
								{
									ImGui::PushID(i);
									const bool item_selected = m_project.items.getTemplates().entry(i).id() == item;
									const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
									if (ImGui::Selectable(item_text, item_selected))
									{
										definition->set_item(m_project.items.getTemplates().entry(i).id());
									}
									if (item_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
									ImGui::PopID();
								}

								ImGui::EndCombo();
							}

							ImGui::TableNextColumn();

							float dropChance = definition->dropchance();
							if (ImGui::SliderFloat("##dropchance", &dropChance, 0.0f, 100.0f, "%.2f%%"))
							{
								definition->set_dropchance(dropChance);
							}

							ImGui::TableNextColumn();

							int minCount = definition->mincount();
							if (ImGui::InputInt("##mincount", &minCount))
							{
								if (minCount < 0) minCount = 0;
								if (minCount > definition->maxcount()) definition->set_maxcount(minCount);
								definition->set_mincount(minCount);
							}

							ImGui::TableNextColumn();

							int maxCount = definition->maxcount();
							if (ImGui::InputInt("##maxcount", &maxCount))
							{
								if (maxCount < 0) maxCount = 0;
								if (maxCount < definition->mincount()) definition->set_mincount(maxCount);
								definition->set_maxcount(maxCount);
							}

							ImGui::TableNextColumn();

							bool active = definition->isactive();
							if (ImGui::Checkbox("##active", &active))
							{
								definition->set_isactive(active);
							}

							ImGui::SameLine();

							ImGui::TableNextColumn();

							if (ImGui::Button("Remove"))
							{
								group.mutable_definitions()->erase(group.mutable_definitions()->begin() + index);
								index--;
							}

							ImGui::PopID();
						}

						ImGui::EndTable();
					}
				}

				ImGui::PopID();
			}
		}
	}
}
