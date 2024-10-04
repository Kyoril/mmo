// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "game_player_s.h"

#include "proto_data/project.h"

namespace mmo
{
	GamePlayerS::GamePlayerS(const proto::Project& project, TimerQueue& timerQueue)
		: GameUnitS(project, timerQueue)
	{
	}

	void GamePlayerS::Initialize()
	{
		GameUnitS::Initialize();

		// Initialize some default values
		Set<int32>(object_fields::MaxLevel, 5, false);
		Set<int32>(object_fields::Xp, 0, false);
		Set<int32>(object_fields::NextLevelXp, 400, false);
		Set<int32>(object_fields::Level, 1, false);
	}

	void GamePlayerS::SetClass(const proto::ClassEntry& classEntry)
	{
		m_classEntry = &classEntry;

		Set<int32>(object_fields::MaxLevel, classEntry.levelbasevalues_size());
		Set<int32>(object_fields::PowerType, classEntry.powertype());
	}

	void GamePlayerS::RewardExperience(const uint32 xp)
	{
		// At max level we can't gain any more xp
		if (Get<uint32>(object_fields::Level) >= Get<uint32>(object_fields::MaxLevel))
		{
			return;
		}

		uint32 currentXp = Get<uint32>(object_fields::Xp);
		currentXp += xp;

		// Levelup as often as required
		while(currentXp > Get<uint32>(object_fields::NextLevelXp))
		{
			currentXp -= Get<uint32>(object_fields::NextLevelXp);
			SetLevel(Get<uint32>(object_fields::Level) + 1);
		}

		// Store remaining xp after potential levelups
		Set<uint32>(object_fields::Xp, currentXp);

		// Send packet

	}

	void GamePlayerS::RefreshStats()
	{
		ASSERT(m_classEntry);

		GameUnitS::RefreshStats();

		// Update all stats
		for (int i = 0; i < 5; ++i)
		{
			UpdateStat(i);
		}

		// Update armor value
		const int32 baseArmor = static_cast<int32>(GetModifierValue(unit_mods::Armor, unit_mod_type::BaseValue));
		const int32 totalArmor = static_cast<int32>(GetModifierValue(unit_mods::Armor, unit_mod_type::TotalValue));
		Set<int32>(object_fields::Armor, baseArmor + totalArmor);
		Set<int32>(object_fields::PosStatArmor, totalArmor > 0 ? totalArmor : 0);
		Set<int32>(object_fields::NegStatArmor, totalArmor < 0 ? totalArmor : 0);

		const int32 level = Get<int32>(object_fields::Level);
		ASSERT(level > 0);
		ASSERT(level <= m_classEntry->levelbasevalues_size());

		// Adjust stats
		const auto* levelStats = &m_classEntry->levelbasevalues(level - 1);

		// TODO: Apply item stats
		
		// Calculate max health from stats
		uint32 maxHealth = UnitStats::GetMaxHealthFromStamina(Get<uint32>(object_fields::StatStamina));
		maxHealth += levelStats->health();
		Set<uint32>(object_fields::MaxHealth, maxHealth);

		// Ensure health is properly capped by max health
		if (Get<uint32>(object_fields::Health) > maxHealth)
		{
			Set<uint32>(object_fields::Health, maxHealth);
		}

		uint32 maxMana = UnitStats::GetMaxManaFromIntellect(Get<uint32>(object_fields::StatIntellect));
		maxMana += levelStats->mana();
		Set<uint32>(object_fields::MaxMana, maxMana);

		// Ensure mana is properly capped by max health
		if (Get<uint32>(object_fields::Mana) > maxMana)
		{
			Set<uint32>(object_fields::Mana, maxMana);
		}

		UpdateDamage();
	}

	void GamePlayerS::SetLevel(uint32 newLevel)
	{
		// Anything to do?
		if (newLevel == 0)
		{
			return;
		}

		// Exceed max level?
		if (newLevel > Get<int32>(object_fields::MaxLevel))
		{
			newLevel = Get<int32>(object_fields::MaxLevel);
		}

		// Adjust stats
		const auto* levelStats = &m_classEntry->levelbasevalues(newLevel - 1);
		SetModifierValue(GetUnitModByStat(0), unit_mod_type::BaseValue, levelStats->stamina());
		SetModifierValue(GetUnitModByStat(1), unit_mod_type::BaseValue, levelStats->strength());
		SetModifierValue(GetUnitModByStat(2), unit_mod_type::BaseValue, levelStats->agility());
		SetModifierValue(GetUnitModByStat(3), unit_mod_type::BaseValue, levelStats->intellect());
		SetModifierValue(GetUnitModByStat(4), unit_mod_type::BaseValue, levelStats->spirit());

		GameUnitS::SetLevel(newLevel);

		// Update next level xp
		Set<uint32>(object_fields::NextLevelXp, 400 * newLevel);
	}

	void GamePlayerS::UpdateDamage()
	{
		float minDamage= 1.0f;
		float maxDamage = 2.0f;

		// TODO: Derive min and max damage from wielded weapon if any

		float baseValue = 0.0f;

		// Calculate base value base on class
		if (m_classEntry)
		{
			baseValue = static_cast<float>(Get<uint32>(object_fields::Level)) * m_classEntry->attackpowerperlevel();

			// Apply stat values
			for(int i = 0; i < m_classEntry->attackpowerstatsources_size(); ++i)
			{
				const auto& statSource = m_classEntry->attackpowerstatsources(i);
				if (statSource.statid() < 5)
				{
					baseValue += static_cast<float>(Get<uint32>(object_fields::StatStamina + statSource.statid())) * statSource.factor();
				}
			}

			baseValue += m_classEntry->attackpoweroffset();
		}

		Set<float>(object_fields::AttackPower, baseValue);

		// 1 dps per 14 attack power
		const float attackTime = Get<uint32>(object_fields::BaseAttackTime) / 1000.0f;
		baseValue = baseValue / 14.0f * attackTime;

		Set<float>(object_fields::MinDamage, baseValue + minDamage);
		Set<float>(object_fields::MaxDamage, baseValue + maxDamage);
	}

	void GamePlayerS::UpdateStat(int32 stat)
	{
		// Validate stat
		if (stat > 4)
		{
			return;
		}

		// Determine unit mod
		const UnitMods mod = GetUnitModByStat(stat);

		// Calculate values
		const float baseVal = GetModifierValue(mod, unit_mod_type::BaseValue);
		const float basePct = GetModifierValue(mod, unit_mod_type::BasePct);
		const float totalVal = GetModifierValue(mod, unit_mod_type::TotalValue);
		const float totalPct = GetModifierValue(mod, unit_mod_type::TotalPct);

		const float value = (baseVal * basePct + totalVal) * totalPct;
		Set<int32>(object_fields::StatStamina + stat, static_cast<int32>(value));
		Set<int32>(object_fields::PosStatStamina + stat, totalVal > 0 ? static_cast<int32>(totalVal) : 0);
		Set<int32>(object_fields::NegStatStamina + stat, totalVal < 0 ? static_cast<int32>(totalVal) : 0);
	}

	io::Writer& operator<<(io::Writer& w, GamePlayerS const& object)
	{
		// Write super class data
		w << reinterpret_cast<GameUnitS const&>(object);

		return w;
	}

	io::Reader& operator>>(io::Reader& r, GamePlayerS& object)
	{
		// Read super class data
		r >> reinterpret_cast<GameUnitS&>(object);

		return r;
	}
}
