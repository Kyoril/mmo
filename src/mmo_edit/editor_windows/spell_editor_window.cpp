// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_editor_window.h"

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
	static String s_powerTypes[] = {
		"Mana",
		"Rage",
		"Energy"
	};

	static String s_spellSchoolNames[] = {
		"Physical",
		"Holy",
		"Fire",
		"Nature",
		"Frost",
		"Shadow",
		"Arcane"
	};

	static String s_spellEffectNames[] = {
		"None",
		"Instakill",
		"School Damage",
		"Dummy",
		"Portal Teleport",
		"Teleport Units",
		"Apply Aura",
		"Environmental Damage",
		"Power Drain",
		"Health Leech",
		"Heal",
		"Bind",
		"Portal",
		"Quest Complete",
		"Weapon Damage + (noschool)",
		"Resurrect",
		"Extra Attacks",
		"Dodge",
		"Evade",
		"Parry",
		"Block",
		"Create Item",
		"Weapon",
		"Defense",
		"Persistent Area Aura",
		"Summon",
		"Leap",
		"Energize",
		"Weapon % Dmg",
		"Trigger Missile",
		"Open Lock",
		"Learn Spell",
		"Spell Defense",
		"Dispel",
		"Language",
		"Dual Wield",
		"Teleport Units Face Caster",
		"Skill Step",
		"Spawn",
		"Trade Skill",
		"Stealth",
		"Detect",
		"Tame Creature",
		"Summon Pet",
		"Learn Pet Spell",
		"Weapon Damage +",
		"Reset Attribute Points",
		"Heal Percentage",
		"Charge",
		"Apply Area Aura",
	};

	static_assert(std::size(s_spellEffectNames) == spell_effects::Count_, "Each spell effect must have a string representation!");
	
	static String s_auraTypeNames[] = {
		"None",
		"Dummy",
		"PeriodicHeal",
		"ModAttackSpeed",
		"ModDamageDone",
		"ModDamageTaken",
		"ModHealth",
		"ModMana", 
		"ModResistance",
		"PeriodicTriggerSpell",
		"PeriodicEnergize",
		"ModStat",
		"ProcTriggerSpell",
															 
		"PeriodicDamage",
		"ModIncreaseSpeed",
		"ModDecreaseSpeed",
		"ModSpeedAlways",	
		"ModSpeedNonStacking",
		"AddFlatModifier",
		"AddPctModifier",

		"ModHealingDone",
		"ModAttackPower",

		"ModHealingTaken",

		"ModDamageDonePct",
		"ModDamageTakenPct"
	};

	static_assert(std::size(s_auraTypeNames) == aura_type::Count_, "Each aura type must have a string representation!");

	static String s_modifierNames[] = {
		"Damage",
		"Duration",
		"Threat",
		"Charges",
		"Range",
		"Radius",
		"CritChance",
		"AllEffects",
		"PreventSpellDelay",
		"CastTime",
		"Cooldown",
		"Cost",
		"CritDamageBonus",
		"ResistMissChance",
		"ChanceOfSuccess",
		"ActivationTime",
		"GlobalCooldown",
		"BonusDamage",
		"PeriodicBasePoints",
		"ResistDispelChance"
	};

	static_assert(std::size(s_modifierNames) == spell_mod_op::Count_, "Each spell mod operation must have a string representation!");

	static String s_statNames[] = {
		"Stamina",
		"Strength",
		"Agility",
		"Intellect",
		"Spirit"
	};

	static String s_resistanceNames[] = {
		"Armor",
		"Holy",
		"Fire",
		"Nature",
		"Frost",
		"Shadow",
		"Arcane"
	};

	static String s_effectTargets[] = {
		"Caster",
		"Nearby Enemy",
		"Nearby Party",
		"Nearby Ally",
		"Pet",
		"Target Enemy",
		"Source Area",
		"Target Area",
		"Home",
		"Source Area Enemy",
		"Target Area Enemy",
		"Database Location",
		"Caster Location",
		"Caster Area Party",
		"Target Ally",
		"Object Target",
		"Cone Enemy",
		"Target Any"
	};

	static_assert(std::size(s_effectTargets) == spell_effect_targets::Count_, "One string per effect target has to exist!");

	SpellEditorWindow::SpellEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.spells, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = true;
		m_toolbarButtonText = "Spells";

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for(const auto& filename : files)
		{
			if (filename.ends_with(".htex") && filename.starts_with("Interface/Icon"))
			{
				m_textures.push_back(filename);
			}
		}
	}

	void SpellEditorWindow::DrawDetailsImpl(proto::SpellEntry& currentEntry)
	{
		if (ImGui::Button("New Rank"))
		{
			if (!currentEntry.has_baseid() || currentEntry.baseid() == 0)
			{
				currentEntry.set_baseid(currentEntry.id());
			}

			if (!currentEntry.has_rank() || currentEntry.rank() < 1)
			{
				currentEntry.set_rank(1);
			}

			proto::SpellEntry* copied = m_project.spells.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
			copied->set_rank(currentEntry.rank() + 1);
			copied->set_baseid(currentEntry.baseid());
		}

		ImGui::SameLine();

		if (ImGui::Button("Duplicate Spell"))
		{
			proto::SpellEntry* copied = m_project.spells.add();
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


		// Migration: Ensure spell has at least one attribute bitmap
		if (currentEntry.attributes_size() < 1)
		{
			currentEntry.add_attributes(0);
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

			int32 rank = currentEntry.rank();
			if (ImGui::InputInt("Rank", &rank))
			{
				currentEntry.set_rank(rank);
			}

			int32 baseId = currentEntry.baseid();
			if (ImGui::InputInt("Base Spell", &baseId))
			{
				currentEntry.set_baseid(baseId);
			}

			uint64 familyFlags = currentEntry.familyflags();
			if (ImGui::InputScalar("Family Flags", ImGuiDataType_U64, &familyFlags, nullptr, nullptr, "0x%016X"))
			{
				currentEntry.set_familyflags(familyFlags);
			}

			ImGui::InputTextMultiline("Description", currentEntry.mutable_description());
			ImGui::InputTextMultiline("Aura Text", currentEntry.mutable_auratext());

			int currentSchool = currentEntry.spellschool();
			if (ImGui::Combo("Spell School", &currentSchool, [](void*, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellSchoolNames))
					{
						return false;
					}

					*out_text = s_spellSchoolNames[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_spellSchoolNames), -1))
			{
				currentEntry.set_spellschool(currentSchool);
			}
		}

		if (ImGui::CollapsingHeader("Casting", ImGuiTreeNodeFlags_None))
		{
			SLIDER_UINT32_PROP(cost, "Cost", 0, 100000);

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
						currentEntry.set_powertype(i);
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			SLIDER_UINT32_PROP(baselevel, "Base Level", 0, 100);
			SLIDER_UINT32_PROP(spelllevel, "Spell Level", 0, 100);
			SLIDER_UINT32_PROP(maxlevel, "Max Level", 0, 100);

			SLIDER_UINT64_PROP(cooldown, "Cooldown", 0, 1000000);
			SLIDER_UINT32_PROP(casttime, "Cast Time (ms)", 0, 100000);
			SLIDER_FLOAT_PROP(speed, "Speed (m/s)", 0, 1000);
			SLIDER_UINT32_PROP(duration, "Duration (ms)", 0, 10000000);

			const bool hasRange = currentEntry.has_rangetype();
			const proto::RangeType* currentRange = hasRange ? m_project.ranges.getById(currentEntry.rangetype()) : nullptr;
			const String rangePreview = currentRange ? currentRange->internalname() : "<None>";

			if (ImGui::BeginCombo("Range", rangePreview.c_str(), ImGuiComboFlags_None))
			{
				ImGui::PushID(-1);
				if (ImGui::Selectable("<None>", !hasRange))
				{
					currentEntry.clear_rangetype();
				}
				if (!hasRange)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();

				for (const auto& range : m_project.ranges.getTemplates().entry())
				{
					ImGui::PushID(range.id());
					const bool item_selected = currentRange && currentRange->id() == range.id();
					const char* item_text = range.internalname().c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_rangetype(range.id());
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

			if (ImGui::Button("Edit Ranges"))
			{
				// TODO: Open popup
			}

			ImGui::Text("Facing flags");

			CHECKBOX_FLAG_PROP(facing, "Target Must be Infront of Caster", spell_facing_flags::TargetInFront);
			ImGui::SameLine();
			CHECKBOX_FLAG_PROP(facing, "Caster must be behind target", spell_facing_flags::BehindTarget);
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

						bool raceIncluded = (currentEntry.racemask() & (1 << (i - 1))) != 0;
						if (ImGui::Checkbox(race->name().c_str(), &raceIncluded))
						{
							if (raceIncluded)
								currentEntry.set_racemask(currentEntry.racemask() | (1 << (i - 1)));
							else
								currentEntry.set_racemask(currentEntry.racemask() & ~(1 << (i - 1)));
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

						bool classIncluded = (currentEntry.classmask() & (1 << (i - 1))) != 0;
						if (ImGui::Checkbox(classEntry->name().c_str(), &classIncluded))
						{
							if (classIncluded)
								currentEntry.set_classmask(currentEntry.classmask() | (1 << (i - 1)));
							else
								currentEntry.set_classmask(currentEntry.classmask() & ~(1 << (i - 1)));
						}
					}
				}

				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Spell Proc", ImGuiTreeNodeFlags_None))
		{
			SLIDER_UINT32_PROP(procchance, "Chance %", 0, 100);
			SLIDER_UINT32_PROP(proccharges, "Proc Charges", 0, 255);
			SLIDER_UINT32_PROP(proccooldown, "Proc Cooldown (ms)", 0, std::numeric_limits<uint32>::max());

			uint64 procFamily = currentEntry.procfamily();
			if (ImGui::InputScalar("Proc Family", ImGuiDataType_U64, &procFamily, nullptr, nullptr, "0x%016X"))
			{
				currentEntry.set_procfamily(procFamily);
			}

			int currentSchool = currentEntry.procschool();
			if (ImGui::Combo("Proc School", &currentSchool, [](void*, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellSchoolNames))
					{
						return false;
					}

					*out_text = s_spellSchoolNames[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_spellSchoolNames), -1))
			{
				currentEntry.set_procschool(currentSchool);
			}

			CHECKBOX_FLAG_PROP(procflags, "When Unit Was Killed", spell_proc_flags::Killed);
			CHECKBOX_FLAG_PROP(procflags, "When Unit Killed Other Unit", spell_proc_flags::Kill);
			CHECKBOX_FLAG_PROP(procflags, "On Melee Attack Swing Done", spell_proc_flags::DoneMeleeAutoAttack);
			CHECKBOX_FLAG_PROP(procflags, "On Melee Attack Swing Taken", spell_proc_flags::TakenMeleeAutoAttack);
			CHECKBOX_FLAG_PROP(procflags, "DoneSpellMeleeDmgClass", spell_proc_flags::DoneSpellMeleeDmgClass);
			CHECKBOX_FLAG_PROP(procflags, "TakenSpellMeleeDmgClass", spell_proc_flags::TakenSpellMeleeDmgClass);
			CHECKBOX_FLAG_PROP(procflags, "DoneRangedAutoAttack", spell_proc_flags::DoneRangedAutoAttack);
			CHECKBOX_FLAG_PROP(procflags, "TakenRangedAutoAttack", spell_proc_flags::TakenRangedAutoAttack);
			CHECKBOX_FLAG_PROP(procflags, "DoneSpellRangedDmgClass", spell_proc_flags::DoneSpellRangedDmgClass);
			CHECKBOX_FLAG_PROP(procflags, "TakenSpellRangedDmgClass", spell_proc_flags::TakenSpellRangedDmgClass);
			CHECKBOX_FLAG_PROP(procflags, "DoneSpellNoneDmgClassPos", spell_proc_flags::DoneSpellNoneDmgClassPos);
			CHECKBOX_FLAG_PROP(procflags, "TakenSpellNoneDmgClassPos", spell_proc_flags::TakenSpellNoneDmgClassPos);
			CHECKBOX_FLAG_PROP(procflags, "DoneSpellNoneDmgClassNeg", spell_proc_flags::DoneSpellNoneDmgClassNeg);
			CHECKBOX_FLAG_PROP(procflags, "TakenSpellNoneDmgClassNeg", spell_proc_flags::TakenSpellNoneDmgClassNeg);
			CHECKBOX_FLAG_PROP(procflags, "DoneSpellMagicDmgClassPos", spell_proc_flags::DoneSpellMagicDmgClassPos);
			CHECKBOX_FLAG_PROP(procflags, "TakenSpellMagicDmgClassPos", spell_proc_flags::TakenSpellMagicDmgClassPos);
			CHECKBOX_FLAG_PROP(procflags, "DoneSpellMagicDmgClassNeg", spell_proc_flags::DoneSpellMagicDmgClassNeg);
			CHECKBOX_FLAG_PROP(procflags, "TakenSpellMagicDmgClassNeg", spell_proc_flags::TakenSpellMagicDmgClassNeg);
			CHECKBOX_FLAG_PROP(procflags, "On Periodic Damage Done", spell_proc_flags::DonePeriodic);
			CHECKBOX_FLAG_PROP(procflags, "On Periodic Damage Taken", spell_proc_flags::TakenPeriodic);
			CHECKBOX_FLAG_PROP(procflags, "On Damage Taken", spell_proc_flags::TakenDamage);
			CHECKBOX_FLAG_PROP(procflags, "When Main Hand Weapon Attack Performed", spell_proc_flags::DoneMainhandAttack);
			CHECKBOX_FLAG_PROP(procflags, "When Off Hand Weapon Attack Performed", spell_proc_flags::DoneOffhandAttack);
			CHECKBOX_FLAG_PROP(procflags, "On Death", spell_proc_flags::Death);
		}

		if (ImGui::CollapsingHeader("Interrupt##header", ImGuiTreeNodeFlags_None))
		{
			CHECKBOX_FLAG_PROP(interruptflags, "Movement", spell_interrupt_flags::Movement);
			CHECKBOX_FLAG_PROP(interruptflags, "Auto Attack", spell_interrupt_flags::AutoAttack);
			CHECKBOX_FLAG_PROP(interruptflags, "Damage", spell_interrupt_flags::Damage);
			CHECKBOX_FLAG_PROP(interruptflags, "Push Back", spell_interrupt_flags::PushBack);
			CHECKBOX_FLAG_PROP(interruptflags, "Interrupt##flag", spell_interrupt_flags::Interrupt);
		}

		if (ImGui::CollapsingHeader("Aura Interrupt Flags", ImGuiTreeNodeFlags_None))
		{
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Hit By Spell", spell_aura_interrupt_flags::HitBySpell);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Damaged By Any Damage", spell_aura_interrupt_flags::Damage);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Crowd Controlled", spell_aura_interrupt_flags::CrowdControl);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Moving", spell_aura_interrupt_flags::Move);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Turning", spell_aura_interrupt_flags::Turning);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Entering Combat", spell_aura_interrupt_flags::EnterCombat);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Unmounted", spell_aura_interrupt_flags::NotMounted);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Swimming", spell_aura_interrupt_flags::NotAboveWater);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When No Longer Swimming", spell_aura_interrupt_flags::NotUnderWater);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Unsheathing", spell_aura_interrupt_flags::NotSheathed);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Talking to an Npc", spell_aura_interrupt_flags::Talk);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Interacting with a Game Object", spell_aura_interrupt_flags::Use);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Auto Attacking", spell_aura_interrupt_flags::Attack);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Spell Casting", spell_aura_interrupt_flags::Cast);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Transforming", spell_aura_interrupt_flags::Transform);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Mounting", spell_aura_interrupt_flags::Mount);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Standing Up", spell_aura_interrupt_flags::NotSeated);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Map Changed", spell_aura_interrupt_flags::ChangeMap);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Unattackable", spell_aura_interrupt_flags::Unattackable);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Teleported", spell_aura_interrupt_flags::Teleported);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Flagged For PvP", spell_aura_interrupt_flags::EnterPvPCombat);
			CHECKBOX_FLAG_PROP(aurainterruptflags, "When Directly Damaged", spell_aura_interrupt_flags::DirectDamage);
		}

		if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_None))
		{
			CHECKBOX_ATTR_PROP(0, "Channeled", spell_attributes::Channeled);
			CHECKBOX_ATTR_PROP(0, "Ranged", spell_attributes::Ranged);
			CHECKBOX_ATTR_PROP(0, "On Next Swing", spell_attributes::OnNextSwing);
			CHECKBOX_ATTR_PROP(0, "Only One Stack Total", spell_attributes::OnlyOneStackTotal);
			CHECKBOX_ATTR_PROP(0, "Ability", spell_attributes::Ability);
			CHECKBOX_ATTR_PROP(0, "Trade Spell", spell_attributes::TradeSpell);
			CHECKBOX_ATTR_PROP(0, "Passive", spell_attributes::Passive);
			CHECKBOX_ATTR_PROP(0, "Hidden On Client", spell_attributes::HiddenClientSide);
			CHECKBOX_ATTR_PROP(0, "Hidden Cast Time", spell_attributes::HiddenCastTime);
			CHECKBOX_ATTR_PROP(0, "Target MainHand Item", spell_attributes::TargetMainhandItem);
			CHECKBOX_ATTR_PROP(0, "Can Target Dead", spell_attributes::CanTargetDead);
			CHECKBOX_ATTR_PROP(0, "Only Daytime", spell_attributes::DaytimeOnly);
			CHECKBOX_ATTR_PROP(0, "Only Night", spell_attributes::NightOnly);
			CHECKBOX_ATTR_PROP(0, "Only Indoor", spell_attributes::IndoorOnly);
			CHECKBOX_ATTR_PROP(0, "Only Outdoor", spell_attributes::OutdoorOnly);
			CHECKBOX_ATTR_PROP(0, "Not Shapeshifted", spell_attributes::NotShapeshifted);
			CHECKBOX_ATTR_PROP(0, "Only Stealthed", spell_attributes::OnlyStealthed);
			CHECKBOX_ATTR_PROP(0, "Dont Sheath", spell_attributes::DontAffectSheathState);
			CHECKBOX_ATTR_PROP(0, "Level Damage Calc", spell_attributes::LevelDamageCalc);
			CHECKBOX_ATTR_PROP(0, "Stop Auto Attack", spell_attributes::StopAttackTarget);
			CHECKBOX_ATTR_PROP(0, "No Defense", spell_attributes::NoDefense);
			CHECKBOX_ATTR_PROP(0, "Track Target", spell_attributes::CastTrackTarget);
			CHECKBOX_ATTR_PROP(0, "Castable While Dead", spell_attributes::CastableWhileDead);
			CHECKBOX_ATTR_PROP(0, "Castable While Mounted", spell_attributes::CastableWhileMounted);
			CHECKBOX_ATTR_PROP(0, "Disabled While Active", spell_attributes::DisabledWhileActive);
			CHECKBOX_ATTR_PROP(0, "Castable While Sitting", spell_attributes::CastableWhileSitting);
			CHECKBOX_ATTR_PROP(0, "Negative", spell_attributes::Negative);
			CHECKBOX_ATTR_PROP(0, "Not In Combat", spell_attributes::NotInCombat);
			CHECKBOX_ATTR_PROP(0, "Ignore Invulnerabiltiy", spell_attributes::IgnoreInvulnerability);
			CHECKBOX_ATTR_PROP(0, "Breakable By Damage", spell_attributes::BreakableByDamage);
			CHECKBOX_ATTR_PROP(0, "Cant Cancel", spell_attributes::CantCancel);
		}

		static bool s_spellClientVisible = false;
		if (ImGui::CollapsingHeader("Client Only", ImGuiTreeNodeFlags_None))
		{
			if (!currentEntry.icon().empty())
			{
				if (!m_iconCache.contains(currentEntry.icon()))
				{
					m_iconCache[currentEntry.icon()] = TextureManager::Get().CreateOrRetrieve(currentEntry.icon());
				}

				if (const TexturePtr tex = m_iconCache[currentEntry.icon()])
				{
					ImGui::Image(tex->GetTextureObject(), ImVec2(64, 64));
				}
			}

			if (ImGui::BeginCombo("Icon", currentEntry.icon().c_str(), ImGuiComboFlags_None))
			{
				for (int i = 0; i < m_textures.size(); i++)
				{
					ImGui::PushID(i);
					const bool item_selected = m_textures[i] == currentEntry.icon();
					const char* item_text = m_textures[i].c_str();
					if (ImGui::Selectable(item_text, item_selected))
					{
						currentEntry.set_icon(item_text);
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}

				ImGui::EndCombo();
			}

			if (ImGui::BeginDragDropTarget())
			{
				// We only accept mesh file drops
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".htex"))
				{
					currentEntry.set_icon(*static_cast<String*>(payload->Data));
				}

				ImGui::EndDragDropTarget();
			}
		}

		if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_None))
		{
			ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);
			for (int effectIndex = 0; effectIndex < currentEntry.effects_size(); ++effectIndex)
			{
				// Effect frame
				int currentEffect = currentEntry.effects(effectIndex).type();
				ImGui::PushID(effectIndex);
				if (ImGui::Combo("Effect", &currentEffect,
					[](void* data, int idx, const char** out_text)
					{
						if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellEffectNames))
						{
							return false;
						}

						*out_text = s_spellEffectNames[idx].c_str();
						return true;
					}, nullptr, IM_ARRAYSIZE(s_spellEffectNames)))
				{
					currentEntry.mutable_effects(effectIndex)->set_type(currentEffect);
				}
				ImGui::SameLine();
				if (ImGui::Button("Details"))
				{
					ImGui::OpenPopup("SpellEffectDetails");
				}
				ImGui::SameLine();

				if (ImGui::Button("Remove"))
				{
					currentEntry.mutable_effects()->DeleteSubrange(effectIndex, 1);
					effectIndex--;
				}
				else
				{
					DrawEffectDialog(currentEntry, *currentEntry.mutable_effects(effectIndex), effectIndex);
				}
				
				ImGui::PopID();
			}

			// Add button
			if (ImGui::Button("Add Effect", ImVec2(-1, 0)))
			{
				currentEntry.add_effects()->set_index(currentEntry.effects_size() - 1);
			}

			ImGui::EndChildFrame();
		}
	}

	static bool IsValidTeleportLocation(uint32 target)
	{
		return target == spell_effect_targets::Home || target == spell_effect_targets::CasterLocation || target == spell_effect_targets::DatabaseLocation;

	}

	void SpellEditorWindow::DrawEffectDialog(proto::SpellEntry& currentEntry, proto::SpellEffect& effect, int32 effectIndex)
	{
		if (ImGui::BeginPopupModal("SpellEffectDetails", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking))
		{
			ImGui::Text("%s effect #%d", currentEntry.name().c_str(), effectIndex + 1);

			int currentEffectType = effect.type();
			if (ImGui::Combo("Effect", &currentEffectType,
				[](void* data, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellEffectNames))
					{
						return false;
					}

					*out_text = s_spellEffectNames[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_spellEffectNames)))
			{
				currentEntry.mutable_effects(effectIndex)->set_type(currentEffectType);
			}

			switch(currentEffectType)
			{
			case spell_effects::ApplyAura:
			case spell_effects::ApplyAreaAura:
			case spell_effects::PersistentAreaAura:
				DrawSpellAuraEffectDetails(effect);
				break;
			default:
				break;
			}

			int effectTarget = effect.targeta();
			if (ImGui::Combo("Target", &effectTarget, [](void*, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_effectTargets))
					{
						return false;
					}

					*out_text = s_effectTargets[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_effectTargets), -1))
			{
				effect.set_targeta(effectTarget);
			}

			switch (currentEffectType)
			{
				case spell_effects::TeleportUnits:
				case spell_effects::TeleportUnitsFaceCaster:
				{
					int effectTarget = effect.targetb();
					if (ImGui::Combo("Teleport Target Location", &effectTarget, [](void*, int idx, const char** out_text)
						{
							if (idx < 0 || idx >= IM_ARRAYSIZE(s_effectTargets))
							{
								return false;
							}

							*out_text = s_effectTargets[idx].c_str();
							return true;
						}, nullptr, IM_ARRAYSIZE(s_effectTargets), -1))
					{
						if (IsValidTeleportLocation(effectTarget))
						{
							effect.set_targetb(effectTarget);
						}
					}
				}
				break;
			case spell_effects::CreateItem:
				{
					const uint32 item = effect.itemtype();

					const auto* itemEntry = m_project.items.getById(item);
					if (ImGui::BeginCombo("Item Type", itemEntry != nullptr ? itemEntry->name().c_str() : "None", ImGuiComboFlags_None))
					{
						for (int i = 0; i < m_project.items.count(); i++)
						{
							ImGui::PushID(i);
							const bool item_selected = m_project.items.getTemplates().entry(i).id() == item;
							const char* item_text = m_project.items.getTemplates().entry(i).name().c_str();
							if (ImGui::Selectable(item_text, item_selected))
							{
								effect.set_itemtype(m_project.items.getTemplates().entry(i).id());
							}
							if (item_selected)
							{
								ImGui::SetItemDefaultFocus();
							}
							ImGui::PopID();
						}

						ImGui::EndCombo();
					}
					break;
				}
			case spell_effects::Energize:
			case spell_effects::ApplyAura:
			case spell_effects::ApplyAreaAura:
				if (currentEffectType == spell_effects::Energize || effect.aura() == aura_type::PeriodicEnergize)
				{
					if (ImGui::BeginCombo("Power Type", s_powerTypes[effect.miscvaluea()].c_str(), ImGuiComboFlags_None))
					{
						for (size_t i = 0; i < std::size(s_powerTypes); ++i)
						{
							if (ImGui::Selectable(s_powerTypes[i].c_str(), effect.miscvaluea() == i))
							{
								effect.set_miscvaluea(i);
							}
						}

						ImGui::EndCombo();
					}
				}
				break;
			}

			if (currentEffectType == spell_effects::LearnSpell ||
				(currentEffectType == spell_effects::ApplyAura &&
				(effect.aura() == aura_type::PeriodicTriggerSpell ||
					effect.aura() == aura_type::ProcTriggerSpell)))
			{
				static const char* s_none = "<None>";
				int spell = effect.triggerspell();
				const proto::SpellEntry* spellEntry = m_project.spells.getById(spell);
				if (ImGui::BeginCombo("Spell", spellEntry != nullptr ? spellEntry->name().c_str() : s_none, ImGuiComboFlags_None))
				{
					for (int i = 0; i < m_project.spells.count(); i++)
					{
						ImGui::PushID(i);
						const bool item_selected = m_project.spells.getTemplates().entry(i).id() == spell;
						const char* item_text = m_project.spells.getTemplates().entry(i).name().c_str();
						if (ImGui::Selectable(item_text, item_selected))
						{
							effect.set_triggerspell(m_project.spells.getTemplates().entry(i).id());
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

			ImGui::Text("Points");

			if (ImGui::BeginChildFrame(ImGui::GetID("effectPoints"), ImVec2(-1, 200), ImGuiWindowFlags_AlwaysUseWindowPadding))
			{
				int basePoints = currentEntry.effects(effectIndex).basepoints();
				if (ImGui::InputInt("Base Points", &basePoints))
				{
					currentEntry.mutable_effects(effectIndex)->set_basepoints(basePoints);
				}

				float pointsPerLevel = currentEntry.effects(effectIndex).pointsperlevel();
				if (ImGui::InputFloat("Per Level", &pointsPerLevel))
				{
					currentEntry.mutable_effects(effectIndex)->set_pointsperlevel(pointsPerLevel);
				}

				int diceSides = currentEntry.effects(effectIndex).diesides();
				if (ImGui::InputInt("Dice Sides", &diceSides))
				{
					currentEntry.mutable_effects(effectIndex)->set_diesides(diceSides);
				}

				float dicePerLevel = currentEntry.effects(effectIndex).diceperlevel();
				if (ImGui::InputFloat("Dice per Level", &dicePerLevel))
				{
					currentEntry.mutable_effects(effectIndex)->set_diceperlevel(dicePerLevel);
				}

				float spellPowerScaling = currentEntry.effects(effectIndex).powerbonusfactor();
				if (ImGui::InputFloat("Spell Power Coefficient", &spellPowerScaling))
				{
					currentEntry.mutable_effects(effectIndex)->set_powerbonusfactor(spellPowerScaling);
				}

				static int characterLevel = 1;
				ImGui::SliderInt("Preview Level", &characterLevel, 1, 60);

				// Calculate level scaling
				int level = characterLevel;
				if (level > currentEntry.maxlevel() && currentEntry.maxlevel() > 0)
				{
					level = currentEntry.maxlevel();
				}
				else if (level < currentEntry.baselevel())
				{
					level = currentEntry.baselevel();
				}
				level -= currentEntry.baselevel();

				ImGui::BeginDisabled(true);
				int min = basePoints + level * currentEntry.effects(effectIndex).pointsperlevel() + std::min<int>(1, diceSides + level * currentEntry.effects(effectIndex).diceperlevel());
				int max = basePoints + level * currentEntry.effects(effectIndex).pointsperlevel() + diceSides + level * currentEntry.effects(effectIndex).diceperlevel();
				ImGui::InputInt("Min", &min);
				ImGui::InputInt("Max", &max);
				ImGui::EndDisabled();

				ImGui::EndChildFrame();
			}

			if (ImGui::Button("Close"))
			{
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	void SpellEditorWindow::DrawSpellAuraEffectDetails(proto::SpellEffect& effect)
	{
		int currentAuraType = effect.aura();
		if (ImGui::Combo("Aura", &currentAuraType,
			[](void* data, int idx, const char** out_text)
			{
				if (idx < 0 || idx >= IM_ARRAYSIZE(s_auraTypeNames))
				{
					return false;
				}

				*out_text = s_auraTypeNames[idx].c_str();
				return true;
			}, nullptr, IM_ARRAYSIZE(s_auraTypeNames)))
		{
			effect.set_aura(currentAuraType);
		}

		switch (currentAuraType)
		{
		case aura_type::PeriodicDamage:
		case aura_type::PeriodicHeal:
		case aura_type::PeriodicEnergize:
		case aura_type::PeriodicTriggerSpell:
		{
			int amplitude = effect.amplitude();
			if (ImGui::InputInt("Amplitude (ms)", &amplitude))
			{
				effect.set_amplitude(amplitude);
			}
		}
		break;
		case aura_type::ModStat:
			{
				int currentStat = effect.miscvaluea();
				if (ImGui::Combo("Stat", &currentStat,
					[](void* data, int idx, const char** out_text)
					{
						if (idx < 0 || idx >= IM_ARRAYSIZE(s_statNames))
						{
							return false;
						}

						*out_text = s_statNames[idx].c_str();
						return true;
					}, nullptr, IM_ARRAYSIZE(s_statNames)))
				{
					effect.set_miscvaluea(currentStat);
				}
			}
			break;

		case aura_type::ModResistance:
			{
			int resistanceType = effect.miscvaluea();
			if (ImGui::Combo("Resistance", &resistanceType,
				[](void* data, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_resistanceNames))
					{
						return false;
					}

					*out_text = s_resistanceNames[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_resistanceNames)))
			{
				effect.set_miscvaluea(resistanceType);
			}
			}
			break;

		case aura_type::AddFlatModifier:
		case aura_type::AddPctModifier:
		{
			int modifier = effect.miscvaluea();
			if (ImGui::Combo("Modifier", &modifier,
				[](void* data, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_modifierNames))
					{
						return false;
					}

					*out_text = s_modifierNames[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_modifierNames)))
			{
				effect.set_miscvaluea(modifier);
			}

			uint64 affectMask = effect.affectmask();
			if (ImGui::InputScalar("Affect Mask", ImGuiDataType_U64, &affectMask, nullptr, nullptr, "0x%016X"))
			{
				effect.set_affectmask(affectMask);
			}
		}
		break;
		}

	}
}
