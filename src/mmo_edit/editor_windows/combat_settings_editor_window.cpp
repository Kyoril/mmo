// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "combat_settings_editor_window.h"
#include "editor_imgui_helpers.h"
#include "editor_host.h"

#include <imgui.h>

namespace mmo
{
	CombatSettingsEditorWindow::CombatSettingsEditorWindow(const String& name, proto::Project& project, EditorHost& host)
		: EditorWindowBase(name)
		, m_host(host)
		, m_project(project)
	{
		EditorWindowBase::SetVisible(false);

		m_hasToolbarButton = false;
		m_toolbarButtonText = "Combat Settings";
	}

	bool CombatSettingsEditorWindow::DrawFloatSetting(const char* label, float& value, float defaultValue)
	{
		bool changed = false;

		if (ImGui::InputFloat(label, &value, 0.0f, 0.0f, "%.6f"))
		{
			changed = true;
		}

		ImGui::SameLine();
		DrawHelpMarker((String("Default: ") + std::to_string(defaultValue)).c_str());

		return changed;
	}

	bool CombatSettingsEditorWindow::DrawUint32Setting(const char* label, uint32& value, uint32 defaultValue)
	{
		bool changed = false;

		int intValue = static_cast<int>(value);
		if (ImGui::InputInt(label, &intValue))
		{
			if (intValue >= 0)
			{
				value = static_cast<uint32>(intValue);
				changed = true;
			}
		}

		ImGui::SameLine();
		DrawHelpMarker((String("Default: ") + std::to_string(defaultValue)).c_str());

		return changed;
	}

	bool CombatSettingsEditorWindow::Draw()
	{
		if (!ImGui::Begin(m_name.c_str(), &m_visible))
		{
			ImGui::End();
			return false;
		}

		auto& settings = m_project.combatSettings;

		// Get default values for hints
		const auto& defaults = proto::GetDefaultCombatSettings();

		ImGui::TextWrapped(
			"These settings control the combat formula parameters used by the server. "
			"Changes take effect after saving and restarting the server.");
		ImGui::Separator();
		ImGui::Spacing();

		// Reset All button
		if (DrawDangerButton("Reset All to Defaults", ImVec2(-1, 0)))
		{
			settings = defaults;
		}

		ImGui::Spacing();

		// === Armor Reduction ===
		if (const auto section = ScopedEditorSection("Armor Reduction", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Formula: armor / (armor + constant + level_factor * attacker_level)");
			ImGui::TextDisabled("Result is clamped to [0, max_reduction_pct].");
			ImGui::Spacing();

			float armorConstant = settings.armor_constant();
			if (DrawFloatSetting("Armor Constant", armorConstant, defaults.armor_constant()))
			{
				settings.set_armor_constant(armorConstant);
			}

			float armorLevelFactor = settings.armor_level_factor();
			if (DrawFloatSetting("Armor Level Factor", armorLevelFactor, defaults.armor_level_factor()))
			{
				settings.set_armor_level_factor(armorLevelFactor);
			}

			float maxArmorReduction = settings.max_armor_reduction_pct();
			if (DrawFloatSetting("Max Armor Reduction %", maxArmorReduction, defaults.max_armor_reduction_pct()))
			{
				settings.set_max_armor_reduction_pct(maxArmorReduction);
			}
		}

		// === Melee Crit ===
		if (const auto section = ScopedEditorSection("Melee Critical Strike", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float critMultiplier = settings.melee_crit_multiplier();
			if (DrawFloatSetting("Crit Damage Multiplier", critMultiplier, defaults.melee_crit_multiplier()))
			{
				settings.set_melee_crit_multiplier(critMultiplier);
			}
		}

		// === Crushing Blow ===
		if (const auto section = ScopedEditorSection("Crushing Blow", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Crushing blows can occur when the attacker is significantly higher level than the victim.");
			ImGui::Spacing();

			float crushingMultiplier = settings.crushing_damage_multiplier();
			if (DrawFloatSetting("Damage Multiplier", crushingMultiplier, defaults.crushing_damage_multiplier()))
			{
				settings.set_crushing_damage_multiplier(crushingMultiplier);
			}

			uint32 crushingThreshold = settings.crushing_level_threshold();
			if (DrawUint32Setting("Level Threshold", crushingThreshold, defaults.crushing_level_threshold()))
			{
				settings.set_crushing_level_threshold(crushingThreshold);
			}

			float crushingBase = settings.crushing_base_chance();
			if (DrawFloatSetting("Base Chance %", crushingBase, defaults.crushing_base_chance()))
			{
				settings.set_crushing_base_chance(crushingBase);
			}

			float crushingPerLevel = settings.crushing_chance_per_level();
			if (DrawFloatSetting("Chance per Level %", crushingPerLevel, defaults.crushing_chance_per_level()))
			{
				settings.set_crushing_chance_per_level(crushingPerLevel);
			}

			float crushingMax = settings.crushing_max_chance();
			if (DrawFloatSetting("Max Chance %", crushingMax, defaults.crushing_max_chance()))
			{
				settings.set_crushing_max_chance(crushingMax);
			}
		}

		// === Glancing Blow ===
		if (const auto section = ScopedEditorSection("Glancing Blow", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Glancing blows occur when attacking higher-level targets.");
			ImGui::Spacing();

			float glancingBase = settings.glancing_base_chance();
			if (DrawFloatSetting("Base Chance %", glancingBase, defaults.glancing_base_chance()))
			{
				settings.set_glancing_base_chance(glancingBase);
			}

			float glancingPerLevel = settings.glancing_chance_per_level();
			if (DrawFloatSetting("Chance per Level %", glancingPerLevel, defaults.glancing_chance_per_level()))
			{
				settings.set_glancing_chance_per_level(glancingPerLevel);
			}

			float glancingMax = settings.glancing_max_chance();
			if (DrawFloatSetting("Max Chance %", glancingMax, defaults.glancing_max_chance()))
			{
				settings.set_glancing_max_chance(glancingMax);
			}

			float reductionPerDiff = settings.glancing_reduction_per_skill_diff();
			if (DrawFloatSetting("Reduction per Skill Diff", reductionPerDiff, defaults.glancing_reduction_per_skill_diff()))
			{
				settings.set_glancing_reduction_per_skill_diff(reductionPerDiff);
			}

			float maxReduction = settings.glancing_max_reduction();
			if (DrawFloatSetting("Max Reduction %", maxReduction, defaults.glancing_max_reduction()))
			{
				settings.set_glancing_max_reduction(maxReduction);
			}
		}

		// === Block ===
		if (const auto section = ScopedEditorSection("Block", ImGuiTreeNodeFlags_DefaultOpen))
		{
			uint32 blockValue = settings.default_block_value();
			if (DrawUint32Setting("Default Block Value", blockValue, defaults.default_block_value()))
			{
				settings.set_default_block_value(blockValue);
			}
		}

		// === Rage Generation ===
		if (const auto section = ScopedEditorSection("Rage Generation", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("rage_conversion = quadratic * level^2 + linear * level + constant");
			ImGui::TextDisabled("rage_gained = (damage / rage_conversion) * damage_factor");
			ImGui::Spacing();

			float rageQuadratic = settings.rage_conversion_quadratic();
			if (DrawFloatSetting("Quadratic Coefficient", rageQuadratic, defaults.rage_conversion_quadratic()))
			{
				settings.set_rage_conversion_quadratic(rageQuadratic);
			}

			float rageLinear = settings.rage_conversion_linear();
			if (DrawFloatSetting("Linear Coefficient", rageLinear, defaults.rage_conversion_linear()))
			{
				settings.set_rage_conversion_linear(rageLinear);
			}

			float rageConstant = settings.rage_conversion_constant();
			if (DrawFloatSetting("Constant Term", rageConstant, defaults.rage_conversion_constant()))
			{
				settings.set_rage_conversion_constant(rageConstant);
			}

			float rageFactor = settings.rage_damage_factor();
			if (DrawFloatSetting("Damage Factor", rageFactor, defaults.rage_damage_factor()))
			{
				settings.set_rage_damage_factor(rageFactor);
			}
		}

		// === Auto Attack Timing ===
		if (const auto section = ScopedEditorSection("Auto Attack Timing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			uint32 errorDelay = settings.attack_swing_error_delay_ms();
			if (DrawUint32Setting("Error Retry Delay (ms)", errorDelay, defaults.attack_swing_error_delay_ms()))
			{
				settings.set_attack_swing_error_delay_ms(errorDelay);
			}

			uint32 parryHasteMin = settings.parry_haste_min_swing_ms();
			if (DrawUint32Setting("Parry Haste Min Swing (ms)", parryHasteMin, defaults.parry_haste_min_swing_ms()))
			{
				settings.set_parry_haste_min_swing_ms(parryHasteMin);
			}
		}

		// === Dodge ===
		if (const auto section = ScopedEditorSection("Dodge", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Base chance for any unit to dodge an incoming melee attack, before stat and aura bonuses.");
			ImGui::Spacing();

			float baseDodgeChance = settings.base_dodge_chance();
			if (DrawFloatSetting("Base Dodge Chance %", baseDodgeChance, defaults.base_dodge_chance()))
			{
				settings.set_base_dodge_chance(baseDodgeChance);
			}
		}

		// === Spell Weapon Damage ===
		if (const auto section = ScopedEditorSection("Spell Weapon Damage", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Settings for weapon damage spell effects (e.g. Heroic Strike, Mortal Strike).");
			ImGui::Spacing();

			float spellCritChance = settings.spell_weapon_default_crit_chance();
			if (DrawFloatSetting("Default Crit Chance %", spellCritChance, defaults.spell_weapon_default_crit_chance()))
			{
				settings.set_spell_weapon_default_crit_chance(spellCritChance);
			}

			float spellCritMultiplier = settings.spell_weapon_crit_multiplier();
			if (DrawFloatSetting("Crit Multiplier", spellCritMultiplier, defaults.spell_weapon_crit_multiplier()))
			{
				settings.set_spell_weapon_crit_multiplier(spellCritMultiplier);
			}
		}

		ImGui::End();
		return false;
	}
}
