// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "game_player_s.h"

#include "game_item_s.h"
#include "proto_data/project.h"
#include "game/item.h"

namespace mmo
{
	GamePlayerS::GamePlayerS(const proto::Project& project, TimerQueue& timerQueue)
		: GameUnitS(project, timerQueue)
		, m_inventory(*this)
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

		m_attributePointEnhancements.fill(0);
		m_attributePointsSpent.fill(0);
		for (uint32 i = 0; i < 5; ++i)
		{
			SetAttributeCost(i, 1);
		}

		Set<uint32>(object_fields::AvailableAttributePoints, 0, false);
	}

	void GamePlayerS::SetClass(const proto::ClassEntry& classEntry)
	{
		m_classEntry = &classEntry;

		Set<int32>(object_fields::MaxLevel, classEntry.levelbasevalues_size());
		Set<int32>(object_fields::PowerType, classEntry.powertype());

		RefreshStats();
	}

	void GamePlayerS::SetRace(const proto::RaceEntry& raceEntry)
	{
		m_raceEntry = &raceEntry;

		Set<uint32>(object_fields::Race, raceEntry.id());
		Set<uint32>(object_fields::FactionTemplate, raceEntry.factiontemplate());
	}

	void GamePlayerS::SetGender(uint8 gender)
	{
		uint32 bytes = Get<uint32>(object_fields::Bytes);

		// Clear the first byte (gender) and then set the new gender
		bytes &= 0xffffff00; // Clear the first byte (mask with 0s in the gender byte and 1s elsewhere)
		bytes |= static_cast<uint32>(gender); // Set the new gender

		Set<uint32>(object_fields::Bytes, bytes);

		// Update visual
		if (m_raceEntry)
		{
			Set<uint32>(object_fields::DisplayId, gender == 0 ? m_raceEntry->malemodel() : m_raceEntry->femalemodel());
		}
	}

	uint8 GamePlayerS::GetGender() const
	{
		const uint32 bytes = Get<uint32>(object_fields::Bytes);
		return bytes & 0xff; // Return only the first byte (gender)
	}

	void GamePlayerS::SetAttributeCost(uint32 attribute, uint8 cost)
	{
		ASSERT(attribute < 5);

		uint64 attributeCostPacked = Get<uint64>(object_fields::AttributePointCost);

		// Clear the 8-bit segment at the specified index and set the new cost
		attributeCostPacked &= ~(static_cast<uint64>(0xFF) << (attribute * 8)); // Clear the 8-bit slot
		attributeCostPacked |= static_cast<uint64>(cost) << (attribute * 8);    // Set the 8-bit value

		Set<uint64>(object_fields::AttributePointCost, attributeCostPacked);
	}

	uint8 GamePlayerS::GetAttributeCost(uint32 attribute) const
	{
		ASSERT(attribute < 5);

		const uint64 attributeCostPacked = Get<uint64>(object_fields::AttributePointCost);
		return (attributeCostPacked >> (attribute * 8)) & 0xFF;
	}

	void GamePlayerS::ApplyItemStats(GameItemS& item, bool apply)
	{
		const auto& itemEntry = item.GetEntry();

		if (itemEntry.durability() == 0 || item.Get<uint32>(object_fields::Durability) > 0)
		{
			// Apply values
			for (int i = 0; i < itemEntry.stats_size(); ++i)
			{
				const auto& stat = itemEntry.stats(i);
				if (stat.value() != 0)
				{
					switch (stat.type())
					{
					case item_stat::Mana:
						UpdateModifierValue(unit_mods::Mana, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Health:
						UpdateModifierValue(unit_mods::Health, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Agility:
						UpdateModifierValue(unit_mods::StatAgility, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Strength:
						UpdateModifierValue(unit_mods::StatStrength, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Intellect:
						UpdateModifierValue(unit_mods::StatIntellect, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Spirit:
						UpdateModifierValue(unit_mods::StatSpirit, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					case item_stat::Stamina:
						UpdateModifierValue(unit_mods::StatStamina, unit_mod_type::TotalValue, stat.value(), apply);
						break;
					default:
						break;
					}
				}
			}

			std::array<bool, 6> shouldUpdateResi;
			shouldUpdateResi.fill(false);

			if (itemEntry.armor())
			{
				UpdateModifierValue(unit_mods::Armor, unit_mod_type::BaseValue, itemEntry.armor(), apply);
			}

			if (itemEntry.holyres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceHoly, unit_mod_type::TotalValue, itemEntry.holyres(), apply);
				shouldUpdateResi[0] = true;
			}
			if (itemEntry.fireres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceFire, unit_mod_type::TotalValue, itemEntry.fireres(), apply);
				shouldUpdateResi[1] = true;
			}
			if (itemEntry.natureres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceNature, unit_mod_type::TotalValue, itemEntry.natureres(), apply);
				shouldUpdateResi[2] = true;
			}
			if (itemEntry.frostres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceFrost, unit_mod_type::TotalValue, itemEntry.frostres(), apply);
				shouldUpdateResi[3] = true;
			}
			if (itemEntry.shadowres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceShadow, unit_mod_type::TotalValue, itemEntry.shadowres(), apply);
				shouldUpdateResi[4] = true;
			}
			if (itemEntry.arcaneres() != 0)
			{
				UpdateModifierValue(unit_mods::ResistanceArcane, unit_mod_type::TotalValue, itemEntry.arcaneres(), apply);
				shouldUpdateResi[5] = true;
			}

			if (apply)
			{
				SpellTargetMap targetMap;
				targetMap.SetUnitTarget(GetGuid());
				targetMap.SetTargetMap(spell_cast_target_flags::Unit);

				for (auto& spell : item.GetEntry().spells())
				{
					// Trigger == onEquip?
					if (spell.trigger() == item_spell_trigger::OnEquip)
					{
						//CastSpell(targetMap, spell.spell(), 0, item.GetGuid());
					}
				}
			}
			else
			{
				//getAuras().removeAllAurasDueToItem(item.GetGuid());
			}

			UpdateArmor();
			UpdateDamage();
		}
	}

	void GamePlayerS::ResetAttributePoints()
	{
		m_attributePointEnhancements.fill(0);
		m_attributePointsSpent.fill(0);
		RefreshStats();
	}

	uint8 GamePlayerS::CalculateAttributeCost(uint32 pointsSpent)
	{
		const double a = 1.5;  // Initial growth multiplier
		const double b = 2.0;  // Offset for smoothing
		const double c = 0.02; // Exponential growth factor
		const double d = 1.5;  // Exponential growth exponent

		// Calculate cost with both square root and exponential components
		double cost = a * std::sqrt(pointsSpent + b) + c * std::pow(pointsSpent, d);

		// Cap the cost at 255
		return static_cast<uint8>(std::min(cost, 255.0));
	}

	float GamePlayerS::GetUnitMissChance() const
	{
		return GameUnitS::GetUnitMissChance();
	}

	bool GamePlayerS::HasOffhandWeapon() const
	{
		if (m_inventory.GetWeaponByAttackType(weapon_attack::OffhandAttack, false, true))
		{
			return true;
		}

		return GameUnitS::HasOffhandWeapon();
	}

	bool GamePlayerS::AddAttributePoint(uint32 attribute)
	{
		// First determine attribute point cost
		const uint8 cost = CalculateAttributeCost(m_attributePointEnhancements[attribute]);

		// Determine available attribute points
		const uint32 attributePoints = Get<uint32>(object_fields::AvailableAttributePoints);
		if (cost > attributePoints)
		{
			return false;
		}

		// Add point spent
		m_attributePointEnhancements[attribute]++;
		m_attributePointsSpent[attribute] += cost;
		RefreshStats();

		return true;
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
		while(currentXp >= Get<uint32>(object_fields::NextLevelXp))
		{
			if (GetLevel() < Get<uint32>(object_fields::MaxLevel))
			{
				if (m_netUnitWatcher)
				{
					const auto& levelStats = m_classEntry->levelbasevalues(GetLevel() - 1);
					const auto& nextLevelStats = m_classEntry->levelbasevalues(GetLevel());
					m_netUnitWatcher->OnLevelUp(GetLevel() + 1,
						static_cast<int32>(nextLevelStats.health()) - static_cast<int32>(levelStats.health()),
						static_cast<int32>(nextLevelStats.mana()) - static_cast<int32>(levelStats.mana()),
						static_cast<int32>(nextLevelStats.stamina()) - static_cast<int32>(levelStats.stamina()),
						static_cast<int32>(nextLevelStats.strength()) - static_cast<int32>(levelStats.strength()),
						static_cast<int32>(nextLevelStats.agility()) - static_cast<int32>(levelStats.agility()),
						static_cast<int32>(nextLevelStats.intellect()) - static_cast<int32>(levelStats.intellect()),
						static_cast<int32>(nextLevelStats.spirit()) - static_cast<int32>(levelStats.spirit()),
						nextLevelStats.talentpoints(),
						nextLevelStats.attributepoints()
					);
				}
				
				currentXp -= Get<uint32>(object_fields::NextLevelXp);
				SetLevel(GetLevel() + 1);
			}
		}

		// Store remaining xp after potential levelups
		Set<uint32>(object_fields::Xp, currentXp);

		// Send packet
		if (m_netUnitWatcher)
		{
			m_netUnitWatcher->OnXpLog(xp);
		}
	}

	void GamePlayerS::RefreshStats()
	{
		ASSERT(m_classEntry);

		GameUnitS::RefreshStats();

		const int32 level = Get<int32>(object_fields::Level);
		ASSERT(level > 0);
		ASSERT(level <= m_classEntry->levelbasevalues_size());

		// Adjust stats
		UpdateAttributePoints();

		const auto* levelStats = &m_classEntry->levelbasevalues(level - 1);
		SetModifierValue(GetUnitModByStat(0), unit_mod_type::BaseValue, levelStats->stamina() + m_attributePointEnhancements[0]);
		SetModifierValue(GetUnitModByStat(1), unit_mod_type::BaseValue, levelStats->strength() + m_attributePointEnhancements[1]);
		SetModifierValue(GetUnitModByStat(2), unit_mod_type::BaseValue, levelStats->agility() + m_attributePointEnhancements[2]);
		SetModifierValue(GetUnitModByStat(3), unit_mod_type::BaseValue, levelStats->intellect() + m_attributePointEnhancements[3]);
		SetModifierValue(GetUnitModByStat(4), unit_mod_type::BaseValue, levelStats->spirit() + m_attributePointEnhancements[4]);

		// Update all stats
		for (int i = 0; i < 5; ++i)
		{
			UpdateStat(i);
		}

		UpdateArmor();

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

		if (m_classEntry->spiritperhealthregen() != 0.0f)
		{
			m_healthRegenPerTick = (Get<uint32>(object_fields::StatSpirit) / m_classEntry->spiritperhealthregen());
		}
		m_healthRegenPerTick += m_classEntry->healthregenpertick();
		if (m_healthRegenPerTick < 0.0f) m_healthRegenPerTick = 0.0f;
		
		if (m_classEntry->spiritpermanaregen() != 0.0f)
		{
			m_manaRegenPerTick = (Get<uint32>(object_fields::StatSpirit) / m_classEntry->spiritpermanaregen());
		}
		m_manaRegenPerTick += m_classEntry->basemanaregenpertick();
		if (m_manaRegenPerTick < 0.0f) m_manaRegenPerTick = 0.0f;

		UpdateDamage();
	}

	void GamePlayerS::SetLevel(uint32 newLevel)
	{
		// Anything to do?
		if (newLevel == 0)
		{
			return;
		}

		ASSERT(m_classEntry);

		// Exceed max level?
		if (newLevel > Get<int32>(object_fields::MaxLevel))
		{
			newLevel = Get<int32>(object_fields::MaxLevel);
		}

		// Calculate total attribute points available at level
		m_totalAvailablePointsAtLevel = 0;
		for (uint32 i = 0; i < newLevel; ++i)
		{
			m_totalAvailablePointsAtLevel += m_classEntry->levelbasevalues(i).attributepoints();
		}

		// Adjust stats
		GameUnitS::SetLevel(newLevel);

		// Update next level xp
		uint32 xpToNextLevel = 400 * newLevel;		// Dummy default value
		if (newLevel - 1 >= m_classEntry->xptonextlevel_size())
		{
			if (m_classEntry->xptonextlevel_size() == 0)
			{
				WLOG("Class " << m_classEntry->name() << " has no experience points per level set, a default value will be used!");
			}
			else
			{
				WLOG("Class " << m_classEntry->name() << " has no experience points per level set for level " << newLevel << ", value from last level will be used!");
				xpToNextLevel = m_classEntry->xptonextlevel(m_classEntry->xptonextlevel_size() - 1);
			}
		}
		else
		{
			xpToNextLevel = m_classEntry->xptonextlevel(newLevel - 1);
		}

		Set<uint32>(object_fields::NextLevelXp, xpToNextLevel);
		
	}

	void GamePlayerS::UpdateDamage()
	{
		uint32 attackSpeed = 2000;
		float minDamage= 1.0f;
		float maxDamage = 2.0f;

		// Derive min and max damage from wielded weapon if any
		std::shared_ptr<GameItemS> mainHandWeapon = m_inventory.GetWeaponByAttackType(weapon_attack::BaseAttack, true, true);
		if (mainHandWeapon != nullptr)
		{
			if (mainHandWeapon->GetEntry().has_damage())
			{
				minDamage = mainHandWeapon->GetEntry().damage().mindmg();
				maxDamage = mainHandWeapon->GetEntry().damage().maxdmg();
			}

			attackSpeed = mainHandWeapon->GetEntry().delay();
			if (attackSpeed == 0)
			{
				WLOG("Weapon " << mainHandWeapon->GetEntry().id() << " [" << mainHandWeapon->GetEntry().name() << "] had a delay of 0, using base attack speed instead!");
				attackSpeed = 2000;
			}
		}

		// Update attack speed value
		Set<uint32>(object_fields::BaseAttackTime, attackSpeed);

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

	void GamePlayerS::UpdateArmor()
	{
		// Update armor value
		int32 baseArmor = static_cast<int32>(GetModifierValue(unit_mods::Armor, unit_mod_type::BaseValue));
		const int32 totalArmor = static_cast<int32>(GetModifierValue(unit_mods::Armor, unit_mod_type::TotalValue));

		// Class
		if (m_classEntry)
		{
			// Apply stat values
			for (int i = 0; i < m_classEntry->armorstatsources_size(); ++i)
			{
				const auto& statSource = m_classEntry->armorstatsources(i);
				if (statSource.statid() < 5)
				{
					baseArmor += static_cast<float>(Get<uint32>(object_fields::StatStamina + statSource.statid())) * statSource.factor();
				}
			}

		}

		Set<int32>(object_fields::Armor, baseArmor + totalArmor);
		Set<int32>(object_fields::PosStatArmor, totalArmor > 0 ? totalArmor : 0);
		Set<int32>(object_fields::NegStatArmor, totalArmor < 0 ? totalArmor : 0);
	}

	void GamePlayerS::UpdateAttributePoints()
	{
		// Calculate available attribute points
		uint32 availableAttributePoints = m_totalAvailablePointsAtLevel;
		for (uint32 i = 0; i < m_attributePointEnhancements.size(); ++i)
		{
			const uint32 realPointsSpent = m_attributePointsSpent[i];
			if (realPointsSpent > availableAttributePoints)
			{
				// Points spent exceeds available points - reset everything!
				WLOG("Points spent is bigger than points available! Resetting spent points...");
				m_attributePointEnhancements.fill(0);
				availableAttributePoints = m_totalAvailablePointsAtLevel;
				break;
			}

			availableAttributePoints -= realPointsSpent;
		}

		// Update available attribute points
		Set<uint32>(object_fields::AvailableAttributePoints, availableAttributePoints);

		// Update attribute point costs
		for (uint32 i = 0; i < m_attributePointEnhancements.size(); ++i)
		{
			const uint8 cost = CalculateAttributeCost(m_attributePointEnhancements[i]);
			SetAttributeCost(i, cost);
		}
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

	void GamePlayerS::RecalculateTotalAttributePointsConsumed(const uint32 attribute)
	{
		ASSERT(attribute < 5);

		m_attributePointsSpent[attribute] = 0;

		for (uint32 i = 1; i <= m_attributePointEnhancements[i]; ++i)
		{
			m_attributePointsSpent[attribute] += CalculateAttributeCost(i - 1);
		}
	}

	io::Writer& operator<<(io::Writer& w, GamePlayerS const& object)
	{
		// Write super class data
		w << reinterpret_cast<GameUnitS const&>(object);
		w << object.m_inventory;
		w << io::write_range(object.m_attributePointEnhancements);

		// Write known spell ids
		w << io::write<uint32>(object.m_spells.size());
		for (const auto& spell : object.m_spells)
		{
			w << io::write<uint32>(spell->id());
		}

		return w;
	}

	io::Reader& operator>>(io::Reader& r, GamePlayerS& object)
	{
		// Read super class data
		r >> reinterpret_cast<GameUnitS&>(object);
		r >> object.m_inventory;
		r >> io::read_range(object.m_attributePointEnhancements);

		// Read spells
		uint32 spellCount;
		r >> io::read<uint32>(spellCount);
		object.m_spells.clear();
		for (uint32 i = 0; i < spellCount; ++i)
		{
			uint32 spellId;
			r >> io::read<uint32>(spellId);

			const auto* spell = object.GetProject().spells.getById(spellId);
			if (spell)
			{
				object.m_spells.emplace(spell);
			}
		}

		for (uint32 i = 0; i < 5; ++i)
		{
			object.RecalculateTotalAttributePointsConsumed(i);
		}
		
		return r;
	}
}
