// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "spell_editor_window.h"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <map>

#include "assets/asset_registry.h"
#include "editor_imgui_helpers.h"
#include "game/aura.h"
#include "game/item.h"
#include "game/spell.h"
#include "game_server/spells/spell_cast.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "editor_imgui_helpers.h"

namespace mmo
{

	extern std::vector<String> s_itemClassStrings;

	extern std::vector<String> s_itemSubclassWeaponStrings;

	extern std::vector<String> s_itemSubclassArmorStrings;

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

	struct SpellEffectInfo
	{
		const char* name;
		const char* category;
		const char* description;
	};

	static SpellEffectInfo s_spellEffectInfo[] = {
		{ "None", "Utility", "No effect" },
		{ "Instakill", "Damage", "Instantly kills the target" },
		{ "School Damage", "Damage", "Deals damage of a specific school" },
		{ "Dummy", "Utility", "Script-handled effect" },
		{ "Portal Teleport", "Movement", "Creates a portal for teleportation" },
		{ "Teleport Units", "Movement", "Teleports units to a location" },
		{ "Apply Aura", "Buff/Debuff", "Applies an aura effect over time" },
		{ "Environmental Damage", "Damage", "Deals environmental damage" },
		{ "Power Drain", "Resource", "Drains mana/energy from target" },
		{ "Health Leech", "Damage", "Steals health from target" },
		{ "Heal", "Healing", "Restores health to target" },
		{ "Bind", "Movement", "Binds player to current location" },
		{ "Portal", "Movement", "Opens a portal" },
		{ "Quest Complete", "Utility", "Marks quest as complete" },
		{ "Weapon Damage + (noschool)", "Damage", "Weapon damage without school" },
		{ "Resurrect", "Healing", "Brings target back to life" },
		{ "Extra Attacks", "Combat", "Grants additional attacks" },
		{ "Dodge", "Combat", "Forces target to dodge" },
		{ "Evade", "Combat", "Makes target evade attacks" },
		{ "Parry", "Combat", "Forces target to parry" },
		{ "Block", "Combat", "Forces target to block" },
		{ "Create Item", "Utility", "Creates an item in inventory" },
		{ "Weapon", "Combat", "Weapon-based effect" },
		{ "Defense", "Combat", "Modifies defense" },
		{ "Persistent Area Aura", "Buff/Debuff", "Creates persistent area aura" },
		{ "Summon", "Summon", "Summons a creature" },
		{ "Leap", "Movement", "Makes unit leap to location" },
		{ "Energize", "Resource", "Restores mana/energy" },
		{ "Weapon % Dmg", "Damage", "Percentage of weapon damage" },
		{ "Trigger Missile", "Combat", "Launches a missile" },
		{ "Open Lock", "Utility", "Opens locked objects" },
		{ "Learn Spell", "Utility", "Teaches a spell" },
		{ "Spell Defense", "Combat", "Improves spell resistance" },
		{ "Dispel", "Utility", "Removes auras from target" },
		{ "Language", "Utility", "Teaches a language" },
		{ "Dual Wield", "Combat", "Enables dual wielding" },
		{ "Teleport Units Face Caster", "Movement", "Teleports target facing caster" },
		{ "Skill Step", "Utility", "Increases skill level" },
		{ "Spawn", "Summon", "Spawns an object/unit" },
		{ "Trade Skill", "Utility", "Modifies trade skill" },
		{ "Stealth", "Utility", "Puts unit in stealth" },
		{ "Detect", "Utility", "Detects stealthed units" },
		{ "Tame Creature", "Utility", "Tames a creature" },
		{ "Summon Pet", "Summon", "Summons player's pet" },
		{ "Learn Pet Spell", "Utility", "Teaches pet a spell" },
		{ "Weapon Damage +", "Damage", "Weapon damage with bonus" },
		{ "Reset Attribute Points", "Utility", "Resets character attributes" },
		{ "Heal Percentage", "Healing", "Heals by percentage of max health" },
		{ "Charge", "Movement", "Charges at target" },
		{ "Apply Area Aura", "Buff/Debuff", "Applies aura in an area" },
		{ "Interrupt Spell Cast", "Combat", "Interrupts spell casting" },
		{ "Reset Talents", "Utility", "Resets talent points" },
		{ "Proficiency", "Utility", "Grants weapon proficiency" }
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
		"Interrupt Spell Cast",
		"Reset Talents",
		"Proficiency"
	};

	static_assert(std::size(s_spellEffectNames) == spell_effects::Count_, "Each spell effect must have a string representation!");

	static String s_spellMechanicNames[] = {
		"None",
		"Charm",
		"Disorient",
		"Disarm",
		"Distract",
		"Fear",
		"Root",
		"Silence",
		"Sleep",
		"Snare",
		"Stun",
		"Freeze",
		"Knockout",
		"Bleed",
		"Polymorph",
		"Banish",
		"Shield",
		"Mount",
		"Daze"
	};

	static_assert(std::size(s_spellMechanicNames) == spell_mechanic::Count_, "Each spell mechanic must have a string representation!");

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
		"ModDamageTakenPct",

		"ModRoot",
		"ModSleep",
		"ModStun",
		"ModFear",

		"ModVisibility",
		"ModResistancePct",

		"ModStatPct"
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
		"Target Any",
		"Instigator"
	};

	static_assert(std::size(s_effectTargets) == spell_effect_targets::Count_, "One string per effect target has to exist!");

	SpellEditorWindow::SpellEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorEntryWindowBase(project, project.spells, name)
		, m_host(host)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Spells";

		std::vector<std::string> files = AssetRegistry::ListFiles();
		for(const auto& filename : files)
		{
			if (filename.ends_with(".htex") && filename.starts_with("Interface/Icon"))
			{
				m_textures.push_back(filename);
			}
		}

		// Ensure each spell has 2 attribute sets
		for (auto& spell : *m_project.spells.getTemplates().mutable_entry())
		{
			while (spell.attributes_size() < 2)
			{
				spell.add_attributes(0);
			}
		}
	}

	void SpellEditorWindow::DrawDetailsImpl(proto::SpellEntry& currentEntry)
	{
		// Top toolbar with actions
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 0.9f, 0.8f));
		if (ImGui::Button("New Rank", ImVec2(120, 0)))
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
		ImGui::PopStyleColor();

		ImGui::SameLine();
		DrawHelpMarker("Create a new rank of this spell with incremented rank number");

		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 0.8f));
		if (ImGui::Button("Duplicate Spell", ImVec2(140, 0)))
		{
			proto::SpellEntry* copied = m_project.spells.add();
			const uint32 newId = copied->id();
			copied->CopyFrom(currentEntry);
			copied->set_id(newId);
		}
		ImGui::PopStyleColor();

		ImGui::SameLine();
		DrawHelpMarker("Create a complete copy of this spell with a new ID");

		ImGui::Separator();
		ImGui::Spacing();

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
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Spell Identity");

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
			ImGui::InputText("##SpellName", currentEntry.mutable_name());
			ImGui::SameLine();
			ImGui::Text("Spell Name");
			ImGui::SameLine();
			DrawHelpMarker("The display name of the spell");

			ImGui::BeginDisabled(true);
			String idString = std::to_string(currentEntry.id());
			ImGui::SetNextItemWidth(100);
			ImGui::InputText("##ID", &idString);
			ImGui::SameLine();
			ImGui::Text("Spell ID");
			ImGui::EndDisabled();

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Spell Hierarchy");

			int32 rank = currentEntry.rank();
			ImGui::SetNextItemWidth(100);
			if (ImGui::InputInt("##Rank", &rank))
			{
				currentEntry.set_rank(rank);
			}
			ImGui::SameLine();
			ImGui::Text("Rank");
			ImGui::SameLine();
			DrawHelpMarker("Spell rank (0 for base rank)");

			int32 baseId = currentEntry.baseid();
			ImGui::SetNextItemWidth(100);
			if (ImGui::InputInt("##BaseSpell", &baseId))
			{
				currentEntry.set_baseid(baseId);
			}
			ImGui::SameLine();
			ImGui::Text("Base Spell ID");
			ImGui::SameLine();
			DrawHelpMarker("ID of the base spell this is a rank of");

			uint64 familyFlags = currentEntry.familyflags();
			ImGui::SetNextItemWidth(200);
			if (ImGui::InputScalar("##FamilyFlags", ImGuiDataType_U64, &familyFlags, nullptr, nullptr, "0x%016X"))
			{
				currentEntry.set_familyflags(familyFlags);
			}
			ImGui::SameLine();
			ImGui::Text("Family Flags");
			ImGui::SameLine();
			DrawHelpMarker("Spell family flags for spell interactions");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Descriptions");

			ImGui::Text("Description");
			ImGui::SameLine();
			DrawHelpMarker("Spell description shown to players");
			ImGui::InputTextMultiline("##Description", currentEntry.mutable_description(), ImVec2(-1, 60));

			ImGui::Spacing();
			ImGui::Text("Aura Text");
			ImGui::SameLine();
			DrawHelpMarker("Text displayed for aura effects");
			ImGui::InputTextMultiline("##AuraText", currentEntry.mutable_auratext(), ImVec2(-1, 60));

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Spell Classification");

			int currentSchool = currentEntry.spellschool();
			ImGui::SetNextItemWidth(200);
			if (ImGui::Combo("##SpellSchool", &currentSchool, [](void*, int idx, const char** out_text)
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
			ImGui::SameLine();
			ImGui::Text("Spell School");
			ImGui::SameLine();
			DrawHelpMarker("Damage/healing type of the spell");

			int currentMechanic = currentEntry.mechanic();
			ImGui::SetNextItemWidth(200);
			if (ImGui::Combo("##SpellMechanic", &currentMechanic, [](void*, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= IM_ARRAYSIZE(s_spellMechanicNames))
					{
						return false;
					}

					*out_text = s_spellMechanicNames[idx].c_str();
					return true;
				}, nullptr, IM_ARRAYSIZE(s_spellMechanicNames), -1))
			{
				currentEntry.set_mechanic(currentMechanic);
			}
			ImGui::SameLine();
			ImGui::Text("Spell Mechanic");
			ImGui::SameLine();
			DrawHelpMarker("Special mechanic type (CC, etc.)");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Casting", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Resource Cost");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(cost, "##Cost", 0, 100000);
			ImGui::SameLine();
			ImGui::Text("Cost");
			ImGui::SameLine();
			DrawHelpMarker("Resource cost to cast this spell");

			int32 powerType = currentEntry.powertype();
			ImGui::SetNextItemWidth(150);
			if (ImGui::BeginCombo("##PowerType", s_powerTypes[powerType].c_str(), ImGuiComboFlags_None))
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
			ImGui::SameLine();
			ImGui::Text("Power Type");
			ImGui::SameLine();
			DrawHelpMarker("Type of resource consumed");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Level Requirements");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(baselevel, "##BaseLevel", 0, 100);
			ImGui::SameLine();
			ImGui::Text("Base Level");
			ImGui::SameLine();
			DrawHelpMarker("Base level for spell scaling");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(spelllevel, "##SpellLevel", 0, 100);
			ImGui::SameLine();
			ImGui::Text("Spell Level");
			ImGui::SameLine();
			DrawHelpMarker("Required character level to learn");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(maxlevel, "##MaxLevel", 0, 100);
			ImGui::SameLine();
			ImGui::Text("Max Level");
			ImGui::SameLine();
			DrawHelpMarker("Maximum level for scaling (0 = no cap)");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Casting Properties");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT64_PROP(cooldown, "##Cooldown", 0, 1000000);
			ImGui::SameLine();
			ImGui::Text("Cooldown (ms)");
			ImGui::SameLine();
			DrawHelpMarker("Time before spell can be cast again");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(casttime, "##CastTime", 0, 100000);
			ImGui::SameLine();
			ImGui::Text("Cast Time (ms)");
			ImGui::SameLine();
			DrawHelpMarker("Time required to cast the spell");

			ImGui::SetNextItemWidth(150);
			SLIDER_FLOAT_PROP(speed, "##Speed", 0, 1000);
			ImGui::SameLine();
			ImGui::Text("Speed (m/s)");
			ImGui::SameLine();
			DrawHelpMarker("Projectile speed for missile spells");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(duration, "##Duration", 0, 10000000);
			ImGui::SameLine();
			ImGui::Text("Duration (ms)");
			ImGui::SameLine();
			DrawHelpMarker("How long the spell effect lasts");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(maxtargets, "##MaxTargets", 0, 100);
			ImGui::SameLine();
			ImGui::Text("Max Targets");
			ImGui::SameLine();
			DrawHelpMarker("Maximum number of targets affected");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Range & Targeting");

			const bool hasRange = currentEntry.has_rangetype();
			const proto::RangeType* currentRange = hasRange ? m_project.ranges.getById(currentEntry.rangetype()) : nullptr;
			const String rangePreview = currentRange ? currentRange->internalname() : "<None>";

			ImGui::SetNextItemWidth(250);
			if (ImGui::BeginCombo("##Range", rangePreview.c_str(), ImGuiComboFlags_None))
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
			ImGui::Text("Range");
			ImGui::SameLine();
			DrawHelpMarker("Casting range configuration");

			ImGui::SameLine();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.7f, 0.8f));
			if (ImGui::Button("Edit Ranges"))
			{
				// TODO: Open popup
			}
			ImGui::PopStyleColor();

			ImGui::SetNextItemWidth(150);
			SLIDER_FLOAT_PROP(threat_multiplier, "##ThreatMultiplier", 0.0f, 100000.0f);
			ImGui::SameLine();
			ImGui::Text("Threat Multiplier");
			ImGui::SameLine();
			DrawHelpMarker("Threat generation modifier");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Facing Requirements");

			CHECKBOX_FLAG_PROP(facing, "Target Must be In Front of Caster", spell_facing_flags::TargetInFront);
			ImGui::SameLine();
			DrawHelpMarker("Spell requires target to be in front");

			CHECKBOX_FLAG_PROP(facing, "Caster Must be Behind Target", spell_facing_flags::BehindTarget);
			ImGui::SameLine();
			DrawHelpMarker("Spell requires caster behind target");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Classes / Races", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
			ImGui::TextWrapped("If none are checked, all races and classes can use this spell.");
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

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Items", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Item Requirements");

			// Create an imgui dropdown for itemclass field (enum: item_class::Type)
			int currentItemClass = currentEntry.itemclass();
			ImGui::SetNextItemWidth(200);
			if (ImGui::Combo("##ItemClass", &currentItemClass, [](void*, int idx, const char** out_text)
				{
					if (idx < 0 || idx >= s_itemClassStrings.size())
					{
						return false;
					}
					*out_text = s_itemClassStrings[idx].c_str();
					return true;
				}, nullptr, s_itemClassStrings.size(), -1))
			{
				currentEntry.set_itemclass(currentItemClass);
			}
			ImGui::SameLine();
			ImGui::Text("Item Class");
			ImGui::SameLine();
			DrawHelpMarker("Required item class to cast this spell");

			// Subclass mask (imgui 32 bit hex edit input field for currentEntry.itemsubclassmask())
			uint32 itemSubclassMask = currentEntry.itemsubclassmask();

			ImGui::Spacing();
			ImGui::Spacing();

			// For each subclass, create a checkbox to toggle it's flag. The flag count depends on the item class. We only support weapon and armor subclass flags for now
			if (currentItemClass == item_class::Weapon)
			{
				DrawSectionHeader("Weapon Subclass Requirements");
				if (ImGui::BeginTable("weaponSubclassMask", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
				{
					for (uint32 i = 0; i < s_itemSubclassWeaponStrings.size(); ++i)
					{
						ImGui::TableNextColumn();
						bool subclassIncluded = (itemSubclassMask & (1 << i)) != 0;
						if (ImGui::Checkbox(s_itemSubclassWeaponStrings[i].c_str(), &subclassIncluded))
						{
							if (subclassIncluded)
								itemSubclassMask |= (1 << i);
							else
								itemSubclassMask &= ~(1 << i);

							currentEntry.set_itemsubclassmask(itemSubclassMask); // Update the mask in the entry
						}
					}
					ImGui::EndTable();
				}
			}
			else if (currentItemClass == item_class::Armor)
			{
				DrawSectionHeader("Armor Subclass Requirements");
				if (ImGui::BeginTable("armorSubclassMask", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
				{
					for (uint32 i = 0; i < s_itemSubclassArmorStrings.size(); ++i)
					{
						ImGui::TableNextColumn();
						bool subclassIncluded = (itemSubclassMask & (1 << i)) != 0;
						if (ImGui::Checkbox(s_itemSubclassArmorStrings[i].c_str(), &subclassIncluded))
						{
							if (subclassIncluded)
								itemSubclassMask |= (1 << i);
							else
								itemSubclassMask &= ~(1 << i);

							currentEntry.set_itemsubclassmask(itemSubclassMask); // Update the mask in the entry
						}
					}
					ImGui::EndTable();
				}
			}

			if (ImGui::Button("Add Reagent"))
			{
				// Open a popup to select an item from the item registry
				ImGui::OpenPopup("Select Reagent Item");
			}

			// Popup for selecting a reagent item
			if (ImGui::BeginPopup("Select Reagent Item"))
			{
				// Allow item filtering by name
				static char filter[128] = "";
				ImGui::InputText("Filter", filter, sizeof(filter));
				ImGui::Separator();
				ImGui::Text("Available Items:");
				ImGui::Separator();

				// Iterate through all items in the item registry
				// and display them as selectable items
				// If the item name contains the filter string, display it
				// If the item is already in the reagents list, skip it
				for (const auto& item : m_project.items.getTemplates().entry())
				{
					if (std::string(filter).empty() || item.name().find(filter) != std::string::npos)
					{
						bool alreadyExists = false;
						for (const auto& reagent : currentEntry.reagents())
						{
							if (reagent.item() == item.id())
							{
								alreadyExists = true;
								break;
							}
						}
						if (!alreadyExists)
						{
							ImGui::PushID(item.id());
							if (ImGui::Selectable(item.name().c_str()))
							{
								auto* reagent = currentEntry.add_reagents();
								reagent->set_item(item.id());
								reagent->set_count(1); // Default count
								ImGui::CloseCurrentPopup();
							}
							ImGui::PopID();
						}
					}
				}
				ImGui::Spacing();
				if (ImGui::Button("Close", ImVec2(80, 0)))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Clear Filter", ImVec2(100, 0)))
				{
					filter[0] = '\0'; // Clear the filter
				}

				ImGui::SameLine();

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.2f, 0.8f));
				if (ImGui::Button("Reset Reagents", ImVec2(120, 0)))
				{
					currentEntry.clear_reagents();
				}
				ImGui::PopStyleColor();

				ImGui::EndPopup();
			}

			ImGui::Spacing();

			// Add a list of required reagents (items) and count. Present as a table with two columns: Item and Count
			// Allow adding new reagents by clicking a button that opens a popup with a list of items
			if (ImGui::BeginTable("Required Reagents", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableHeadersRow();

				for (int i = 0; i < currentEntry.reagents_size(); ++i)
				{
					ImGui::PushID(i);
					const auto& reagent = currentEntry.mutable_reagents(i);
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (proto::ItemEntry* item = m_project.items.getById(reagent->item()))
					{
						ImGui::Text("%s", item->name().c_str());
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
						ImGui::Text("Unknown Item (%d)", reagent->item());
						ImGui::PopStyleColor();
					}
					ImGui::TableSetColumnIndex(1);

					int count = reagent->count();
					if (ImGui::InputInt("##count", &count, 1, 100, ImGuiInputTextFlags_CharsDecimal))
					{
						reagent->set_count(count);
					}

					ImGui::TableSetColumnIndex(2);
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
					if (ImGui::Button("Remove", ImVec2(-1, 0)))
					{
						currentEntry.mutable_reagents()->erase(currentEntry.mutable_reagents()->begin() + i);
						// Break the loop to avoid invalidating the iterator
						ImGui::PopStyleColor();
						ImGui::PopID();
						break;
					}
					ImGui::PopStyleColor();

					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			else if (currentEntry.reagents_size() == 0)
			{
				ImGui::TextDisabled("No reagents required. Click 'Add Reagent' to add one.");
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Spell Proc", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Proc Configuration");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(procchance, "##ProcChance", 0, 100);
			ImGui::SameLine();
			ImGui::Text("Proc Chance %%");
			ImGui::SameLine();
			DrawHelpMarker("Percentage chance for the spell to proc");

			ImGui::SetNextItemWidth(100);
			SLIDER_UINT32_PROP(proccharges, "##ProcCharges", 0, 255);
			ImGui::SameLine();
			ImGui::Text("Proc Charges");
			ImGui::SameLine();
			DrawHelpMarker("Number of times the spell can proc before being consumed");

			ImGui::SetNextItemWidth(150);
			SLIDER_UINT32_PROP(proccooldown, "##ProcCooldown", 0, std::numeric_limits<uint32>::max());
			ImGui::SameLine();
			ImGui::Text("Proc Cooldown (ms)");
			ImGui::SameLine();
			DrawHelpMarker("Minimum time between procs");

			uint64 procFamily = currentEntry.procfamily();
			ImGui::SetNextItemWidth(200);
			if (ImGui::InputScalar("##ProcFamily", ImGuiDataType_U64, &procFamily, nullptr, nullptr, "0x%016X"))
			{
				currentEntry.set_procfamily(procFamily);
			}
			ImGui::SameLine();
			ImGui::Text("Proc Family");
			ImGui::SameLine();
			DrawHelpMarker("Spell family mask for proc triggers");

			int currentSchool = currentEntry.procschool();
			ImGui::SetNextItemWidth(150);
			if (ImGui::Combo("##ProcSchool", &currentSchool, [](void*, int idx, const char** out_text)
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
			ImGui::SameLine();
			ImGui::Text("Proc School");
			ImGui::SameLine();
			DrawHelpMarker("Spell school that triggers this proc");

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Proc Trigger Conditions");

			CHECKBOX_FLAG_PROP(procflags, "When Owner Was Killed", spell_proc_flags::Killed);
			CHECKBOX_FLAG_PROP(procflags, "When Owner Killed Other Unit", spell_proc_flags::Kill);
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
			CHECKBOX_FLAG_PROP(procflags, "On Periodic Damage Done", spell_proc_flags::DonePeriodicDamage);
			CHECKBOX_FLAG_PROP(procflags, "On Periodic Damage Taken", spell_proc_flags::TakenPeriodicDamage);
			CHECKBOX_FLAG_PROP(procflags, "On Damage Taken", spell_proc_flags::TakenDamage);
			CHECKBOX_FLAG_PROP(procflags, "When Main Hand Weapon Attack Performed", spell_proc_flags::DoneMainhandAttack);
			CHECKBOX_FLAG_PROP(procflags, "When Off Hand Weapon Attack Performed", spell_proc_flags::DoneOffhandAttack);
			CHECKBOX_FLAG_PROP(procflags, "On Death", spell_proc_flags::Death);

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Additional Proc Conditions");

			CHECKBOX_FLAG_PROP(procexflags, "Normal Hit", spell_proc_flags_ex::NormalHit);
			CHECKBOX_FLAG_PROP(procexflags, "Critical Hit", spell_proc_flags_ex::CriticalHit);
			CHECKBOX_FLAG_PROP(procexflags, "Absorb", spell_proc_flags_ex::Absorb);
			CHECKBOX_FLAG_PROP(procexflags, "Block", spell_proc_flags_ex::Block);
			CHECKBOX_FLAG_PROP(procexflags, "Deflect", spell_proc_flags_ex::Deflect);
			CHECKBOX_FLAG_PROP(procexflags, "Dodge", spell_proc_flags_ex::Dodge);
			CHECKBOX_FLAG_PROP(procexflags, "Evade", spell_proc_flags_ex::Evade);
			CHECKBOX_FLAG_PROP(procexflags, "Immune", spell_proc_flags_ex::Immune);
			CHECKBOX_FLAG_PROP(procexflags, "Interrupt", spell_proc_flags_ex::Interrupt);
			CHECKBOX_FLAG_PROP(procexflags, "Miss", spell_proc_flags_ex::Miss);
			CHECKBOX_FLAG_PROP(procexflags, "Parry", spell_proc_flags_ex::Parry);
			CHECKBOX_FLAG_PROP(procexflags, "Reflect", spell_proc_flags_ex::Reflect);
			CHECKBOX_FLAG_PROP(procexflags, "Resist", spell_proc_flags_ex::Resist);

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Interrupt##header", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Cast Interrupt Conditions");

			CHECKBOX_FLAG_PROP(interruptflags, "Movement", spell_interrupt_flags::Movement);
			ImGui::SameLine();
			DrawHelpMarker("Spell is interrupted by movement");

			CHECKBOX_FLAG_PROP(interruptflags, "Auto Attack", spell_interrupt_flags::AutoAttack);
			ImGui::SameLine();
			DrawHelpMarker("Spell is interrupted by auto-attacking");

			CHECKBOX_FLAG_PROP(interruptflags, "Damage", spell_interrupt_flags::Damage);
			ImGui::SameLine();
			DrawHelpMarker("Spell is interrupted by taking damage");

			CHECKBOX_FLAG_PROP(interruptflags, "Push Back", spell_interrupt_flags::PushBack);
			ImGui::SameLine();
			DrawHelpMarker("Spell casting can be pushed back");

			CHECKBOX_FLAG_PROP(interruptflags, "Interrupt##flag", spell_interrupt_flags::Interrupt);
			ImGui::SameLine();
			DrawHelpMarker("Spell can be interrupted by interrupt effects");

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Aura Interrupt Flags", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Aura Removal Conditions");

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

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Spell Attributes");

			if (ImGui::BeginTable("AttributesTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
			{
				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Channeled", spell_attributes::Channeled);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Ranged", spell_attributes::Ranged);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "On Next Swing", spell_attributes::OnNextSwing);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Only One Stack Total", spell_attributes::OnlyOneStackTotal);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Ability", spell_attributes::Ability);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Trade Spell", spell_attributes::TradeSpell);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Passive", spell_attributes::Passive);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Hidden On Client", spell_attributes::HiddenClientSide);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Hidden Cast Time", spell_attributes::HiddenCastTime);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Target MainHand Item", spell_attributes::TargetMainhandItem);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Can Target Dead", spell_attributes::CanTargetDead);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Only Daytime", spell_attributes::DaytimeOnly);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Only Night", spell_attributes::NightOnly);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Only Indoor", spell_attributes::IndoorOnly);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Only Outdoor", spell_attributes::OutdoorOnly);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Not Shapeshifted", spell_attributes::NotShapeshifted);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Only Stealthed", spell_attributes::OnlyStealthed);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Dont Sheath", spell_attributes::DontAffectSheathState);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Level Damage Calc", spell_attributes::LevelDamageCalc);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Stop Auto Attack", spell_attributes::StopAttackTarget);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "No Defense", spell_attributes::NoDefense);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Track Target", spell_attributes::CastTrackTarget);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Castable While Dead", spell_attributes::CastableWhileDead);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Castable While Mounted", spell_attributes::CastableWhileMounted);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Disabled While Active", spell_attributes::DisabledWhileActive);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Castable While Sitting", spell_attributes::CastableWhileSitting);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Negative", spell_attributes::Negative);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Not In Combat", spell_attributes::NotInCombat);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Ignore Invulnerabiltiy", spell_attributes::IgnoreInvulnerability);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Breakable By Damage", spell_attributes::BreakableByDamage);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(0, "Cant Cancel", spell_attributes::CantCancel);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(1, "Start Melee Combat", spell_attributes_b::MeleeCombatStart);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(1, "Aura is Hidden", spell_attributes_b::HiddenAura);

				ImGui::TableNextColumn();
				CHECKBOX_ATTR_PROP(1, "Is Talent", spell_attributes_b::Talent);

				ImGui::EndTable();
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		static bool s_spellClientVisible = false;
		if (ImGui::CollapsingHeader("Client Only", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Spell Icon");

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

			ImGui::Spacing();
			ImGui::Spacing();
			DrawSectionHeader("Spell Visualization");
			
			// Visualization ID dropdown
			int visualizationId = currentEntry.has_visualization_id() ? static_cast<int>(currentEntry.visualization_id()) : 0;
			
			if (ImGui::BeginCombo("Spell Visualization", visualizationId > 0 ? std::to_string(visualizationId).c_str() : "None (use icon)"))
			{
				// "None" option to clear visualization
				if (ImGui::Selectable("None (use icon)", visualizationId == 0))
				{
					currentEntry.clear_visualization_id();
				}
				if (visualizationId == 0)
				{
					ImGui::SetItemDefaultFocus();
				}
				
				// List all available visualizations
				for (int i = 0; i < m_project.spellVisualizations.count(); ++i)
				{
					const auto& vis = m_project.spellVisualizations.getTemplates().entry().at(i);
					ImGui::PushID(i);
					
					char label[256];
					snprintf(label, sizeof(label), "%u: %s", vis.id(), vis.name().c_str());
					
					const bool item_selected = visualizationId == vis.id();
					if (ImGui::Selectable(label, item_selected))
					{
						currentEntry.set_visualization_id(vis.id());
					}
					if (item_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
				
				ImGui::EndCombo();
			}
			
			if (currentEntry.has_visualization_id() && visualizationId > 0)
			{
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.2f, 0.8f));
				if (ImGui::SmallButton("Clear"))
				{
					currentEntry.clear_visualization_id();
				}
				ImGui::PopStyleColor();
			}

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_None))
		{
			ImGui::Indent();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Spell Effects");

			ImGui::BeginChildFrame(ImGui::GetID("effectsBorder"), ImVec2(-1, 400), ImGuiWindowFlags_AlwaysUseWindowPadding);
			for (int effectIndex = 0; effectIndex < currentEntry.effects_size(); ++effectIndex)
			{
				// Effect frame
				int currentEffect = currentEntry.effects(effectIndex).type();
				ImGui::PushID(effectIndex);

				// Searchable effect dropdown
				static char effectSearchBuffer[128] = "";
				bool effectChanged = false;

				ImGui::SetNextItemWidth(300);
				if (ImGui::BeginCombo("Effect", s_spellEffectInfo[currentEffect].name, ImGuiComboFlags_HeightLarge))
				{
				// Fixed search bar at the top
				ImGui::SetNextItemWidth(-1);
				if (ImGui::InputText("##effectsearch", effectSearchBuffer, IM_ARRAYSIZE(effectSearchBuffer)))
				{
					// Auto-focus search on typing
				}
				ImGui::Separator();

				// Scrollable region for the effect list
				if (ImGui::BeginChild("##effectlist", ImVec2(0, 300), false))
				{
					// Collect items by category
					std::map<String, std::vector<int>> categorizedEffects;
					for (int idx = 0; idx < IM_ARRAYSIZE(s_spellEffectInfo); ++idx)
					{
						// Filter based on search
						if (effectSearchBuffer[0] != '\0')
						{
							String searchLower = effectSearchBuffer;
							String nameLower = s_spellEffectInfo[idx].name;
							String categoryLower = s_spellEffectInfo[idx].category;
							std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
							std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
							std::transform(categoryLower.begin(), categoryLower.end(), categoryLower.begin(), ::tolower);

							if (nameLower.find(searchLower) == String::npos &&
								categoryLower.find(searchLower) == String::npos)
							{
								continue;
							}
						}

						categorizedEffects[s_spellEffectInfo[idx].category].push_back(idx);
					}

					// Draw effects grouped by category
					for (auto& [category, indices] : categorizedEffects)
					{
						// When searching, expand all categories automatically
						ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_DefaultOpen;
						if (effectSearchBuffer[0] != '\0')
						{
							treeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
						}

						if (ImGui::TreeNodeEx(category.c_str(), treeFlags))
						{
							for (int idx : indices)
							{
								const bool isSelected = (idx == currentEffect);
								
								if (ImGui::Selectable(s_spellEffectInfo[idx].name, isSelected))
								{
									currentEntry.mutable_effects(effectIndex)->set_type(idx);
									effectChanged = true;
									effectSearchBuffer[0] = '\0';
								}

								if (isSelected)
								{
									ImGui::SetItemDefaultFocus();
								}

								// Show description as tooltip
								if (ImGui::IsItemHovered())
								{
									ImGui::BeginTooltip();
									ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
									ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%s", s_spellEffectInfo[idx].name);
									ImGui::Separator();
									ImGui::TextWrapped("%s", s_spellEffectInfo[idx].description);
									ImGui::PopTextWrapPos();
									ImGui::EndTooltip();
								}
							}
							
							ImGui::TreePop();
						}
					}
				}
				ImGui::EndChild();

				ImGui::EndCombo();
			}

			// Show current effect description
			ImGui::SameLine();
			DrawHelpMarker(s_spellEffectInfo[currentEffect].description);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.7f, 0.8f));
			if (ImGui::Button("Details", ImVec2(80, 0)))
			{
				ImGui::OpenPopup("SpellEffectDetails");
			}
			ImGui::PopStyleColor();
			ImGui::SameLine();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
			if (ImGui::Button("Remove", ImVec2(80, 0)))
			{
				currentEntry.mutable_effects()->DeleteSubrange(effectIndex, 1);
				ImGui::PopStyleColor();
				effectIndex--;
			}
			else
			{
				ImGui::PopStyleColor();
				DrawEffectDialog(currentEntry, *currentEntry.mutable_effects(effectIndex), effectIndex);
			}
				
				ImGui::PopID();
			}

			// Add button
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 0.8f));
			if (ImGui::Button("+ Add Effect", ImVec2(-1, 0)))
			{
				currentEntry.add_effects()->set_index(currentEntry.effects_size() - 1);
			}
			ImGui::PopStyleColor();

			ImGui::EndChildFrame();

			ImGui::PopStyleVar(2);
			ImGui::Unindent();
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
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

			DrawSectionHeader("Effect Configuration");
			ImGui::Text("Spell: %s (Effect #%d)", currentEntry.name().c_str(), effectIndex + 1);
			ImGui::Spacing();

			int currentEffectType = effect.type();

			// Effect type info box
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.2f, 0.25f, 0.5f));
			if (ImGui::BeginChild("effectinfo", ImVec2(500, 60), true))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
				ImGui::Text("%s", s_spellEffectInfo[currentEffectType].name);
				ImGui::PopStyleColor();

				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Category: %s", s_spellEffectInfo[currentEffectType].category);
				ImGui::TextWrapped("%s", s_spellEffectInfo[currentEffectType].description);
				ImGui::EndChild();
			}
			ImGui::PopStyleColor();

			ImGui::Spacing();
			DrawSectionHeader("Core Settings");

			// Targeting
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

			float radius = effect.radius();
			if (ImGui::DragFloat("Radius", &radius, 0.5f, 0.0f, 1000.0f, "%.1f yards"))
			{
				effect.set_radius(radius);
			}
			ImGui::SameLine();
			DrawHelpMarker("Area of effect radius in yards");

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Aura configuration
			if (currentEffectType == spell_effects::ApplyAura ||
				currentEffectType == spell_effects::ApplyAreaAura ||
				currentEffectType == spell_effects::PersistentAreaAura)
			{
				DrawSectionHeader("Aura Configuration");
				DrawSpellAuraEffectDetails(effect);
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
			}

			// Type-specific settings
			DrawSectionHeader("Type-Specific Settings");

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

			if (currentEffectType == spell_effects::ApplyAura ||
				currentEffectType == spell_effects::ApplyAreaAura)
			{
				if (effect.aura() == aura_type::ProcTriggerSpell ||
					effect.aura() == aura_type::PeriodicTriggerSpell)
				{
					int effectTarget = effect.targetb();
					if (ImGui::Combo("Proc Spell Target", &effectTarget, [](void*, int idx, const char** out_text)
						{
							if (idx < 0 || idx >= IM_ARRAYSIZE(s_effectTargets))
							{
								return false;
							}

							*out_text = s_effectTargets[idx].c_str();
							return true;
						}, nullptr, IM_ARRAYSIZE(s_effectTargets), -1))
					{
						effect.set_targetb(effectTarget);
					}
				}
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			DrawSectionHeader("Effect Power");

			if (ImGui::BeginChild("effectPoints", ImVec2(500, 280), true))
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

				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Text("Power Preview");
				ImGui::Spacing();

				static int characterLevel = 1;
				ImGui::SliderInt("Character Level", &characterLevel, 1, 60);

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

				int min = basePoints + level * currentEntry.effects(effectIndex).pointsperlevel() + std::min<int>(1, diceSides + level * currentEntry.effects(effectIndex).diceperlevel());
				int max = basePoints + level * currentEntry.effects(effectIndex).pointsperlevel() + diceSides + level * currentEntry.effects(effectIndex).diceperlevel();

				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.3f, 0.25f, 0.5f));
				ImGui::BeginDisabled(true);
				ImGui::InputInt("Min Value", &min);
				ImGui::InputInt("Max Value", &max);
				ImGui::EndDisabled();
				ImGui::PopStyleColor();

				ImGui::EndChild();
			}

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.6f, 0.9f, 0.8f));
			if (ImGui::Button("Close", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopStyleColor();

			ImGui::PopStyleVar(2);
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
		case aura_type::ModStatPct:
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
		case aura_type::ModResistancePct:
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
