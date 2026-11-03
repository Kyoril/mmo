// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pet_editor_window.h"
#include "editor_imgui_helpers.h"
#include "proto_entry_picker.h"
#include "unit_spell_entry_table.h"

#include <iterator>

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "game/pet_defines.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		const char* const s_nameModes[] = { "Fixed", "Random from list" };
		const char* const s_levelModes[] = { "Own level", "Master level" };
		const char* const s_levelSources[] = { "Creature level", "Character level", "Class level" };

		/// Draws a float field that writes back through a setter.
		template <class Setter>
		void DrawMultiplier(const char* label, const float current, Setter setter)
		{
			float value = current;
			if (ImGui::InputFloat(label, &value, 0.05f, 0.25f, "%.2f"))
			{
				setter(value < 0.0f ? 0.0f : value);
			}
		}
	}

	PetEditorWindow::PetEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.pets, name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Pets";
	}

	void PetEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
	{
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

			int unitEntry = static_cast<int>(currentEntry.unit_entry());
			if (DrawEntryPicker("##unitEntry", "Creature", unitEntry, m_project.units, "The creature template this pet is spawned from."))
			{
				currentEntry.set_unit_entry(static_cast<uint32>(unitEntry));
			}

			int displayId = static_cast<int>(currentEntry.display_id());
			if (DrawEntryPicker("##displayId", "Display Override", displayId, m_project.models, "Model data to use instead of the creature's own model. None keeps the creature's model."))
			{
				currentEntry.set_display_id(static_cast<uint32>(displayId));
			}

			bool allowNaming = currentEntry.allow_player_naming();
			if (ImGui::Checkbox("Allow Player Naming", &allowNaming))
			{
				currentEntry.set_allow_player_naming(allowNaming);
			}
		}

		if (const auto section = ScopedEditorSection("Name", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int nameMode = static_cast<int>(currentEntry.name_mode());
			if (DrawEnumCombo("##nameMode", "Name Mode", nameMode, s_nameModes, static_cast<int>(std::size(s_nameModes)), "Fixed uses the entry name; Random picks one of the names below."))
			{
				currentEntry.set_name_mode(static_cast<uint32>(nameMode));
			}

			ImGui::TextUnformatted("Random Names");
			if (DrawSuccessButton("Add Name", ImVec2(-1, 0)))
			{
				currentEntry.add_random_names("New Name");
			}

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.random_names_size(); ++i)
			{
				ImGui::PushID(i);
				ImGui::SetNextItemWidth(260.0f);
				ImGui::InputText("##randomName", currentEntry.mutable_random_names(i));
				ImGui::SameLine();
				if (DrawDangerButton("Remove"))
				{
					removeIndex = i;
				}
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_random_names()->erase(currentEntry.mutable_random_names()->begin() + removeIndex);
			}

			if (currentEntry.random_names_size() == 0)
			{
				ImGui::TextDisabled("No random names.");
			}
		}

		if (const auto section = ScopedEditorSection("Level", ImGuiTreeNodeFlags_DefaultOpen))
		{
			int levelMode = static_cast<int>(currentEntry.level_mode());
			if (DrawEnumCombo("##levelMode", "Level Mode", levelMode, s_levelModes, static_cast<int>(std::size(s_levelModes)), "Whether the pet levels on its own or follows its master."))
			{
				currentEntry.set_level_mode(static_cast<uint32>(levelMode));
			}

			ImGui::BeginDisabled(levelMode != pet_level_mode::MasterLevel);
			int levelSource = static_cast<int>(currentEntry.level_source());
			if (DrawEnumCombo("##levelSource", "Level Source", levelSource, s_levelSources, static_cast<int>(std::size(s_levelSources)), "Which level of the master the pet follows. Creature level is only valid for temporary pets."))
			{
				currentEntry.set_level_source(static_cast<uint32>(levelSource));
			}
			ImGui::EndDisabled();
		}

		if (const auto section = ScopedEditorSection("Default Spells", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (DrawSuccessButton("Add Spell", ImVec2(-1, 0)))
			{
				currentEntry.add_default_spells(0);
			}

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.default_spells_size(); ++i)
			{
				ImGui::PushID(i);
				int spell = static_cast<int>(currentEntry.default_spells(i));
				if (DrawEntryPicker("##spell", "", spell, m_project.spells))
				{
					currentEntry.set_default_spells(i, static_cast<uint32>(spell));
				}
				ImGui::SameLine();
				if (DrawDangerButton("Remove"))
				{
					removeIndex = i;
				}
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_default_spells()->erase(currentEntry.mutable_default_spells()->begin() + removeIndex);
			}

			if (currentEntry.default_spells_size() == 0)
			{
				ImGui::TextDisabled("No default spells.");
			}
		}

		if (const auto section = ScopedEditorSection("Level Spells", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (DrawSuccessButton("Add Level Spell", ImVec2(-1, 0)))
			{
				auto* levelSpell = currentEntry.add_level_spells();
				levelSpell->set_spell(0);
				levelSpell->set_level(1);
			}

			if (ImGui::BeginTable("levelSpells", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Spell", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				int removeIndex = -1;
				for (int i = 0; i < currentEntry.level_spells_size(); ++i)
				{
					auto* levelSpell = currentEntry.mutable_level_spells(i);
					ImGui::PushID(i);
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					int level = static_cast<int>(levelSpell->level());
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::InputInt("##level", &level))
					{
						levelSpell->set_level(level < 1 ? 1 : static_cast<uint32>(level));
					}

					ImGui::TableNextColumn();
					int spell = static_cast<int>(levelSpell->spell());
					if (DrawEntryPicker("##spell", "", spell, m_project.spells))
					{
						levelSpell->set_spell(static_cast<uint32>(spell));
					}
					ImGui::SameLine();
					if (DrawDangerButton("Remove"))
					{
						removeIndex = i;
					}

					ImGui::PopID();
				}

				ImGui::EndTable();

				if (removeIndex >= 0)
				{
					currentEntry.mutable_level_spells()->erase(currentEntry.mutable_level_spells()->begin() + removeIndex);
				}
			}
		}

		if (const auto section = ScopedEditorSection("AI Spells", ImGuiTreeNodeFlags_None))
		{
			DrawUnitSpellEntryTable("aispells", m_project, *currentEntry.mutable_ai_spells());
		}

		if (const auto section = ScopedEditorSection("Multipliers", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawMultiplier("Health", currentEntry.health_multiplier(), [&](float v) { currentEntry.set_health_multiplier(v); });
			DrawMultiplier("Mana", currentEntry.mana_multiplier(), [&](float v) { currentEntry.set_mana_multiplier(v); });
			DrawMultiplier("Damage", currentEntry.damage_multiplier(), [&](float v) { currentEntry.set_damage_multiplier(v); });
			DrawMultiplier("Armor", currentEntry.armor_multiplier(), [&](float v) { currentEntry.set_armor_multiplier(v); });
		}
	}

	void PetEditorWindow::OnNewEntry(proto::TemplateManager<proto::Pets, proto::PetEntry>::EntryType& entry)
	{
		entry.set_name("New Pet");
		entry.set_unit_entry(0);
		entry.set_name_mode(pet_name_mode::Fixed);
		entry.set_level_mode(pet_level_mode::OwnLevel);
		entry.set_level_source(pet_level_source::CharacterLevel);
		entry.set_allow_player_naming(false);
		entry.set_health_multiplier(1.0f);
		entry.set_mana_multiplier(1.0f);
		entry.set_damage_multiplier(1.0f);
		entry.set_armor_multiplier(1.0f);
	}
}
