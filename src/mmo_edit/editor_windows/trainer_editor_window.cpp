// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "trainer_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/aura.h"
#include "game/spell.h"
#include "game_server/spells/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"

namespace mmo
{
	TrainerEditorWindow::TrainerEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.trainers, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Trainers";
	}

	void TrainerEditorWindow::DrawDetailsImpl(proto::TrainerEntry& currentEntry)
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

#define MONEY_PROP_LABEL(cost) \
	{ \
		const int32 gold = ::floor(cost / 10000); \
		const int32 silver = ::floor(::fmod(cost, 10000) / 100);\
		const int32 copper = ::fmod(cost, 100);\
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

		if (ImGui::CollapsingHeader("Trainer Spells"))
		{
			static const char* s_itemNone = "<None>";

			// Add button
			if (ImGui::Button("Add"))
			{
				auto* newEntry = currentEntry.add_spells();
				newEntry->set_spell(0);
				newEntry->set_spellcost(0);
				newEntry->set_reqlevel(1);
				newEntry->set_reqskill(0);
				newEntry->set_reqskillval(0);
			}

			ImGui::SameLine();

			if (ImGui::Button("Order By Level"))
			{
				std::sort(currentEntry.mutable_spells()->begin(), currentEntry.mutable_spells()->end(), [](const proto::TrainerSpellEntry& a, const proto::TrainerSpellEntry& b)
					{
						return a.reqlevel() < b.reqlevel();
					});
			}

			ImGui::SameLine();

			if (ImGui::Button("Adjust Min Level"))
			{
				// For each spell in the list, adjust the min level to the spells baselevel property
				for (auto& spell : *currentEntry.mutable_spells())
				{
					const auto* spellEntry = m_project.spells.getById(spell.spell());
					if (spellEntry)
					{
						spell.set_reqlevel(spellEntry->spelllevel());
					}
				}
			}

			if (ImGui::BeginTable("vendorspells", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Min Level", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Skill", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Skill Value", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.spells_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_spells(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					const uint32 spell = currentItem->spell();

					const auto* spellEntry = m_project.spells.getById(spell);
					if (ImGui::BeginCombo("##spell", spellEntry != nullptr ? spellEntry->name().c_str() : s_itemNone, ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.spells.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.spells.getTemplates().entry(i).id() == spell;
							const char* item_text = m_project.spells.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentItem->set_spell(m_project.spells.getTemplates().entry(i).id());
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

					int32 cost = currentItem->spellcost();
					if (ImGui::InputInt("##spellcost", &cost))
					{
						currentItem->set_spellcost(cost);
					}

					ImGui::SameLine();

					MONEY_PROP_LABEL(currentItem->spellcost());

					ImGui::TableNextColumn();

					int32 level = currentItem->reqlevel();
					if (ImGui::InputInt("##minlevel", &level))
					{
						currentItem->set_reqlevel(level);
					}

					ImGui::TableNextColumn();

					int32 skill = currentItem->reqskill();
					if (ImGui::InputInt("##skill", &skill))
					{
						currentItem->set_reqskill(skill);
					}

					ImGui::TableNextColumn();

					int32 skillval = currentItem->reqskillval();
					if (ImGui::InputInt("##skillval", &skillval))
					{
						currentItem->set_reqskillval(skillval);
					}

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
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
}
