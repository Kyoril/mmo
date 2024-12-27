// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "creature_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "assets/asset_registry.h"
#include "game/object_type_id.h"
#include "log/default_log_levels.h"

namespace ImGui
{
	void BeginGroupPanel(const char* name, const ImVec2& size = ImVec2(-1.0f, -1.0f));

	void EndGroupPanel();
}

namespace mmo
{
	CreatureEditorWindow::CreatureEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.units, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Creatures";
	}

	void CreatureEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
		if (ImGui::Button("Duplicate Creature"))
		{
			proto::UnitEntry* copied = m_project.units.add();
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

		if (ImGui::CollapsingHeader("Basic", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::BeginTable("table", 4, ImGuiTableFlags_None))
			{
				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Name", currentEntry.mutable_name());
				}

				if (ImGui::TableNextColumn())
				{
					ImGui::InputText("Subname", currentEntry.mutable_subname());
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

			static const char* s_unitLootNone = "<None>";

			uint32 lootEntry = currentEntry.unitlootentry();

			const auto* unitLootEntry = m_project.unitLoot.getById(lootEntry);
			if (ImGui::BeginCombo("Unit Loot Entry", unitLootEntry != nullptr ? unitLootEntry->name().c_str() : s_unitLootNone, ImGuiComboFlags_None))
			{
				ImGui::PushID(-1);
				if (ImGui::Selectable(s_unitLootNone, unitLootEntry == nullptr))
				{
					currentEntry.set_unitlootentry(-1);
				}
				ImGui::PopID();

				for (int i = 0; i < m_project.unitLoot.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.unitLoot.getTemplates().entry(i).id() == lootEntry;
					const char* item_text = m_project.unitLoot.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_unitlootentry(m_project.unitLoot.getTemplates().entry(i).id());
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

		static const char* s_noneEntryString = "<None>";

		if (ImGui::CollapsingHeader("Factions", ImGuiTreeNodeFlags_None))
		{
			int32 factionTemplate = currentEntry.factiontemplate();

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
						currentEntry.set_factiontemplate(m_project.factionTemplates.getTemplates().entry(i).id());
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

		if (ImGui::CollapsingHeader("Npcs", ImGuiTreeNodeFlags_None))
		{
			int32 currentTrainer = currentEntry.trainerentry();

			const auto* trainerEntry = m_project.trainers.getById(currentTrainer);
			if (ImGui::BeginCombo("Trainer", trainerEntry != nullptr ? trainerEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.trainers.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.trainers.getTemplates().entry(i).id() == currentTrainer;
					const char* item_text = m_project.trainers.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_trainerentry(m_project.trainers.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			int32 currentVendor = currentEntry.vendorentry();

			const auto* vendorEntry = m_project.vendors.getById(currentVendor);
			if (ImGui::BeginCombo("Vendor", vendorEntry != nullptr ? vendorEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.vendors.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.vendors.getTemplates().entry(i).id() == currentVendor;
					const char* item_text = m_project.vendors.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_vendorentry(m_project.vendors.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			CHECKBOX_FLAG_PROP(npcflags, "Inn Keeper", npc_flags::InnKeeper);
		}

		if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_None))
		{
			int32 maleModel = currentEntry.malemodel();

			const auto* maleModelEntry = m_project.models.getById(maleModel);
			if (ImGui::BeginCombo("Male Model", maleModelEntry != nullptr ? maleModelEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.models.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.models.getTemplates().entry(i).id() == maleModel;
					const char* item_text = m_project.models.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_malemodel(m_project.models.getTemplates().entry(i).id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			int32 femaleModel = currentEntry.femalemodel();

			const auto* femaleModelEntry = m_project.models.getById(femaleModel);
			if (ImGui::BeginCombo("Female Model", femaleModelEntry != nullptr ? femaleModelEntry->name().c_str() : s_noneEntryString, ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_project.models.count(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_project.models.getTemplates().entry(i).id() == femaleModel;
					const char* item_text = m_project.models.getTemplates().entry(i).name().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_femalemodel(m_project.models.getTemplates().entry(i).id());
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

		if (ImGui::CollapsingHeader("Level & Stats", ImGuiTreeNodeFlags_None))
		{
			ImGui::PushID("Level");
			ImGui::BeginGroupPanel("Level");
			SLIDER_UINT32_PROP(minlevel, "Min", 1, 100);
			SLIDER_UINT32_PROP(maxlevel, "Max", 1, 100);
			ImGui::EndGroupPanel();
			ImGui::PopID();

			ImGui::PushID("Health");
			ImGui::BeginGroupPanel("Health");
			SLIDER_UINT32_PROP(minlevelhealth, "Min", 1, 200000000);
			SLIDER_UINT32_PROP(maxlevelhealth, "Max", 1, 200000000);
			ImGui::EndGroupPanel();
			ImGui::PopID();

			ImGui::PushID("Experience");
			ImGui::BeginGroupPanel("Experience");
			SLIDER_UINT32_PROP(minlevelxp, "Min Level XP", 0, 10000000);
			SLIDER_UINT32_PROP(maxlevelxp, "Max Level XP", 0, 10000000);
			if (ImGui::Button("Calculate XP"))
			{
				const float eliteFactor = currentEntry.rank() > 0 ? 2.0f : 1.0f;
				currentEntry.set_minlevelxp((currentEntry.minlevel() * 5 + 45) * eliteFactor);
				currentEntry.set_maxlevelxp((currentEntry.maxlevel() * 5 + 45) * eliteFactor);
			}
			ImGui::EndGroupPanel();
			ImGui::PopID();

			SLIDER_UINT32_PROP(armor, "Armor", 0, 100000);

			ImGui::BeginGroupPanel("Damage");
			SLIDER_FLOAT_PROP(minmeleedmg, "Min Melee Dmg", 0.0f, 10000000.0f);
			SLIDER_FLOAT_PROP(maxmeleedmg, "Max Melee Dmg", 0.0f, 10000000.0f);
			ImGui::EndGroupPanel();
		}

		if (ImGui::CollapsingHeader("Creature Spells", ImGuiTreeNodeFlags_None))
		{
			static const char* s_itemNone = "<None>";

			// Add button
			if (ImGui::Button("Add", ImVec2(-1, 0)))
			{
				auto* newEntry = currentEntry.add_creaturespells();
				newEntry->set_spellid(0);
			}

			if (ImGui::BeginTable("creaturespells", 7, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_DefaultSort);
				ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Min Initial Cooldown", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Max Initial Cooldown", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Min Cooldown", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Max Cooldown", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Probability", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (int index = 0; index < currentEntry.creaturespells_size(); ++index)
				{
					auto* currentItem = currentEntry.mutable_creaturespells(index);

					ImGui::PushID(index);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();

					const uint32 spell = currentItem->spellid();

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
								currentItem->set_spellid(m_project.spells.getTemplates().entry(i).id());
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

					int32 priority = currentItem->priority();
					if (ImGui::InputInt("##priority", &priority))
					{
						currentItem->set_priority(priority);
					}

					ImGui::TableNextColumn();

					int32 mininitialcooldown = currentItem->mininitialcooldown();
					if (ImGui::InputInt("ms##mininitialcooldown", &mininitialcooldown))
					{
						currentItem->set_mininitialcooldown(mininitialcooldown);
					}

					ImGui::TableNextColumn();

					int32 maxinitialcooldown = currentItem->maxinitialcooldown();
					if (ImGui::InputInt("ms##maxinitialcooldown", &maxinitialcooldown))
					{
						currentItem->set_maxinitialcooldown(maxinitialcooldown);
					}

					ImGui::TableNextColumn();

					int32 mincooldown = currentItem->mincooldown();
					if (ImGui::InputInt("ms##mininitialcooldown", &mincooldown))
					{
						currentItem->set_mincooldown(mincooldown);
					}

					ImGui::TableNextColumn();

					int32 maxcooldown = currentItem->maxcooldown();
					if (ImGui::InputInt("ms##maxinitialcooldown", &maxcooldown))
					{
						currentItem->set_maxcooldown(maxcooldown);
					}

					ImGui::TableNextColumn();

					int32 probability = currentItem->probability();
					if (ImGui::InputInt("%##probability", &probability))
					{
						currentItem->set_probability(probability);
					}

					ImGui::SameLine();

					if (ImGui::Button("Remove"))
					{
						currentEntry.mutable_creaturespells()->erase(currentEntry.mutable_creaturespells()->begin() + index);
						index--;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

	}

	void CreatureEditorWindow::OnNewEntry(proto::TemplateManager<proto::Units, proto::UnitEntry>::EntryType& entry)
	{
		entry.set_minlevel(1);
		entry.set_maxlevel(1);
		entry.set_factiontemplate(0);
		entry.set_malemodel(0);
		entry.set_femalemodel(0);
		entry.set_type(0);
		entry.set_family(0);
	}
}
