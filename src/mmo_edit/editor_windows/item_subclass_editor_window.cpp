// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_subclass_editor_window.h"
#include "editor_imgui_helpers.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "log/default_log_levels.h"

#include "sound_entry_combo.h"
#include "surface_type_combo.h"

namespace mmo
{
	ItemSubclassEditorWindow::ItemSubclassEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.itemSubclasses, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Item Subclasses";
	}

	void ItemSubclassEditorWindow::DrawDetailsImpl(EntryType& currentEntry)
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

				if (ImGui::TableNextColumn())
				{
					uint32 itemClass = currentEntry.itemclass();
					const auto* itemClassEntry = m_project.itemClasses.getById(itemClass);
					if (ImGui::BeginCombo("Item Class", itemClassEntry != nullptr ? itemClassEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.itemClasses.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.itemClasses.getTemplates().entry(i).id() == itemClass;
							const char* item_text = m_project.itemClasses.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.set_itemclass(m_project.itemClasses.getTemplates().entry(i).id());
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

				if (ImGui::TableNextColumn())
				{
					uint32 proficiency = currentEntry.has_requiredproficiency() ? currentEntry.requiredproficiency() : 0;
					const auto* proficiencyEntry = m_project.proficiencies.getById(proficiency);
					if (ImGui::BeginCombo("Required Proficiency", proficiencyEntry != nullptr ? proficiencyEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						// Add "None" option
						if (ImGui::Selectable("None", proficiency == 0))
						{
							currentEntry.clear_requiredproficiency();
						}
						
						for (int i = 0; i < m_project.proficiencies.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.proficiencies.getTemplates().entry(i).id() == proficiency;
							const char* item_text = m_project.proficiencies.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								currentEntry.set_requiredproficiency(m_project.proficiencies.getTemplates().entry(i).id());
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

				ImGui::EndTable();
			}
		}

		if (const auto section = ScopedEditorSection("Attack Animations", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Skeleton animation states used for auto attacks (e.g. \"SwordAttack01\").\nOne is chosen at random per swing. Empty list = unarmed attack.");

			int removeIndex = -1;
			for (int i = 0; i < currentEntry.attackanimation_size(); ++i)
			{
				ImGui::PushID(i);
				ImGui::InputText("##animname", currentEntry.mutable_attackanimation(i));
				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeIndex = i;
				}
				ImGui::PopID();
			}

			if (removeIndex >= 0)
			{
				currentEntry.mutable_attackanimation()->DeleteSubrange(removeIndex, 1);
			}

			if (ImGui::Button("Add Attack Animation"))
			{
				currentEntry.add_attackanimation();
			}

			ImGui::Separator();

			String readyAnimation = currentEntry.has_readyanimation() ? currentEntry.readyanimation() : String();
			if (ImGui::InputText("Ready Animation", &readyAnimation))
			{
				if (readyAnimation.empty())
				{
					currentEntry.clear_readyanimation();
				}
				else
				{
					currentEntry.set_readyanimation(readyAnimation);
				}
			}
			ImGui::TextDisabled("Combat-ready (attack idle) stance held while in combat (e.g. \"SwordReady\"). Empty = unarmed ready.");
		}

		if (const auto section = ScopedEditorSection("Off-Hand Attack Animations", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Skeleton animation states used for off-hand (dual wield) auto attacks.\nOne is chosen at random per off-hand swing. Empty = falls back to the main-hand list, then unarmed.");

			int removeOffhandIndex = -1;
			for (int i = 0; i < currentEntry.offhand_attackanimation_size(); ++i)
			{
				ImGui::PushID(1000 + i);
				ImGui::InputText("##offhandanimname", currentEntry.mutable_offhand_attackanimation(i));
				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeOffhandIndex = i;
				}
				ImGui::PopID();
			}

			if (removeOffhandIndex >= 0)
			{
				currentEntry.mutable_offhand_attackanimation()->DeleteSubrange(removeOffhandIndex, 1);
			}

			if (ImGui::Button("Add Off-Hand Attack Animation"))
			{
				currentEntry.add_offhand_attackanimation();
			}
		}

		if (const auto section = ScopedEditorSection("Combat Sounds", ImGuiTreeNodeFlags_None))
		{
			ImGui::TextDisabled("Auto attack audio. Weapon subclasses fill the swing / impact / crit / miss\nfields, shields fill Block Sound, armor subclasses fill Hit Material.");

			DrawSoundEntryCombo(m_project.sounds, "Swing Sound", currentEntry.swing_sound(), m_swingSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_swing_sound(id); });
			ImGui::TextDisabled("Whoosh played when a swing with this weapon starts. Empty = the attacker model's natural weapon swing.");

			DrawSoundEntryCombo(m_project.sounds, "Crit Layer Sound", currentEntry.crit_layer_sound(), m_critSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_crit_layer_sound(id); });
			ImGui::TextDisabled("Played on top of the impact sound on a critical hit.");

			DrawSoundEntryCombo(m_project.sounds, "Miss Sound", currentEntry.miss_sound(), m_missSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_miss_sound(id); });
			ImGui::TextDisabled("Whiff played when a swing with this weapon misses or is dodged.");

			DrawSoundEntryCombo(m_project.sounds, "Parry Sound", currentEntry.parry_sound(), m_parrySoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_parry_sound(id); });
			ImGui::TextDisabled("Played when a weapon of this subclass parries an incoming swing.");

			DrawSoundEntryCombo(m_project.sounds, "Block Sound", currentEntry.block_sound(), m_blockSoundFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_block_sound(id); });
			ImGui::TextDisabled("Shield subclasses: played when a shield of this subclass blocks a swing.");

			DrawSurfaceTypeCombo(m_project.surfaceTypes, "Hit Material", currentEntry.hit_material(), m_hitMaterialFilter,
				[&currentEntry](const uint32 id) { currentEntry.set_hit_material(id); });
			ImGui::TextDisabled("Armor subclasses: what this armor sounds like when struck. Overrides the wearer's body material.");

			ImGui::Separator();
			ImGui::TextDisabled("Impact sounds by struck material. A row with material \"(Any)\" is the fallback\nused when no other row matches.");

			int removeImpactIndex = -1;
			for (int i = 0; i < currentEntry.impact_sounds_size(); ++i)
			{
				ImGui::PushID(2000 + i);

				auto* impact = currentEntry.mutable_impact_sounds(i);

				DrawSurfaceTypeCombo(m_project.surfaceTypes, "##impactmaterial", impact->target_material(), m_impactMaterialFilter,
					[impact](const uint32 id) { impact->set_target_material(id); }, "(Any)");

				ImGui::SameLine();

				DrawSoundEntryCombo(m_project.sounds, "##impactsound", impact->sound(), m_impactSoundFilter,
					[impact](const uint32 id) { impact->set_sound(id); });

				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					removeImpactIndex = i;
				}

				ImGui::PopID();
			}

			if (removeImpactIndex >= 0)
			{
				currentEntry.mutable_impact_sounds()->DeleteSubrange(removeImpactIndex, 1);
			}

			if (ImGui::Button("Add Impact Sound"))
			{
				currentEntry.add_impact_sounds();
			}
		}
	}

	void ItemSubclassEditorWindow::OnNewEntry(proto::TemplateManager<proto::ItemSubclasses, proto::ItemSubclassEntry>::EntryType& entry)
	{
		entry.set_name("New Item Subclass");
		entry.set_itemclass(0);
	}
}
