// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/objects/game_creature_s.h"

#include "base/utilities.h"
#include "proto_data/project.h"
#include "game_server/world/world_instance.h"
#include "binary_io/vector_sink.h"

namespace mmo
{
	GameCreatureS::GameCreatureS(
		const proto::Project& project,
		TimerQueue& timers,
		const proto::UnitEntry& entry)
		: GameUnitS(project, timers)
		, m_originalEntry(entry)
		, m_entry(nullptr)
	{
	}

	void GameCreatureS::Initialize()
	{
		GameUnitS::Initialize();

		SetRegeneration(m_originalEntry.regeneration());

		// Choose random level between min and maxlevel
		std::uniform_int_distribution levelDist(m_originalEntry.minlevel(), m_originalEntry.maxlevel());
		const uint32 level = levelDist(randomGenerator);

		// Initialize creature based on unit entry values
		Set<uint32>(object_fields::Level, level);
		ClearFieldChanges();

		// Setup AI
		m_ai = make_unique<CreatureAI>(*this, CreatureAI::Home(m_movementInfo.position, m_movementInfo.facing.GetValueRadians()));
	}

	void GameCreatureS::Relocate(const Vector3& position, const Radian& facing)
	{
		GameUnitS::Relocate(position, facing);

		ASSERT(m_ai);
		m_ai->OnControlledMoved();
	}

	void GameCreatureS::SetEntry(const proto::UnitEntry& entry)
	{
		const bool firstInitialization = (m_entry == nullptr);

		// Same entry? Nothing to change
		if (m_entry == &entry)
		{
			return;
		}
		
		if (m_entry)
		{
			// Remove all spells from previous entry
			for (const auto& spell : m_entry->creaturespells())
			{
				RemoveSpell(spell.spellid());
			}
		}

		// Setup new entry
		m_entry = &entry;

		// Add all creature spells
		for (const auto& spell : m_entry->creaturespells())
		{
			AddSpell(spell.spellid());
		}

		// Use base npc flags from entry
		uint32 npcFlags = npc_flags::None;
		if (m_entry->trainerentry())
		{
			npcFlags |= npc_flags::Trainer;
		}
		if (m_entry->vendorentry())
		{
			npcFlags |= npc_flags::Vendor;
		}

		if (m_entry->gossip_menus_size() > 0)
		{
			npcFlags |= npc_flags::Gossip;
		}

		// Creature offers or accepts quests (potentially)
		if (m_entry->quests_size() > 0 || m_entry->end_quests_size() > 0)
		{
			npcFlags |= npc_flags::QuestGiver;
		}
		Set<uint32>(object_fields::NpcFlags, npcFlags);
		
		// For legacy system, set max health/mana from entry
		if (!m_entry->usestatbasedsystem())
		{
			Set<uint32>(object_fields::MaxHealth, m_entry->minlevelhealth());
			Set<uint32>(object_fields::MaxMana, m_entry->minlevelmana());
		}
		
		Set<uint32>(object_fields::Entry, m_entry->id());
		Set<float>(object_fields::Scale, m_entry->scale());
		Set<uint32>(object_fields::DisplayId, m_entry->malemodel());	// TODO: gender roll
		Set<uint32>(object_fields::FactionTemplate, m_entry->factiontemplate());
		
		// For legacy system, set power type to mana
		if (!m_entry->usestatbasedsystem())
		{
			Set<uint32>(object_fields::PowerType, power_type::Mana);
		}
		
		RefreshStats();

		SetRegeneration(m_entry->regeneration());

		if (firstInitialization)
		{
			// Initialize current health and mana using the calculated max values			Set<uint32>(object_fields::Health, static_cast<uint32>(static_cast<float>(GetMaxHealth()) * m_healthPercent));
			Set<uint32>(object_fields::Mana, Get<uint32>(object_fields::MaxMana));
			ClearFieldChanges();
		}

		// Add all required variables
		for (const auto& variable : m_entry->variables())
		{
			AddVariable(variable);
		}
	}

	void GameCreatureS::AddLootRecipient(uint64 guid)
	{
		m_lootRecipients.add(guid);
	}

	void GameCreatureS::RemoveLootRecipients()
	{
		m_lootRecipients.clear();
	}

	bool GameCreatureS::IsLootRecipient(const GamePlayerS& character) const
	{
		return m_lootRecipients.contains(character.GetGuid());
	}

	void GameCreatureS::SetHealthPercent(const float percent)
	{
		ASSERT(percent >= 0.0f && percent <= 1.0f);

		m_healthPercent = percent;
		Set<uint32>(object_fields::Health, static_cast<uint32>(static_cast<float>(GetMaxHealth()) * percent));
	}

	void GameCreatureS::SetUnitLoot(std::unique_ptr<LootInstance> unitLoot)
	{
		m_loot = std::move(unitLoot);

		// This unit is lootable if the unit loot is set
		if (m_loot)
		{
			AddFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
		}
		else
		{
			RemoveFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
		}
	}

	QuestgiverStatus GameCreatureS::GetQuestGiverStatus(const GamePlayerS& player) const
	{
		QuestgiverStatus result = questgiver_status::None;

		for (const auto& quest : GetEntry().end_quests())
		{
			if (const QuestStatus questStatus = player.GetQuestStatus(quest); questStatus == quest_status::Complete)
			{
				return questgiver_status::Reward;
			}
			else if (questStatus == quest_status::Incomplete)
			{
				result = questgiver_status::Incomplete;
			}
		}

		bool hasQuestAvailableNextLevel = false;

		for (const auto& quest : GetEntry().quests())
		{
			const QuestStatus questStatus = player.GetQuestStatus(quest);
			if (questStatus == quest_status::Available)
			{
				if (const proto::QuestEntry* entry = GetProject().quests.getById(quest))
				{
					return questgiver_status::Available;
				}
			}
			else if (questStatus == quest_status::AvailableNextLevel)
			{
				hasQuestAvailableNextLevel = true;
			}
		}

		// Check if there will be quests available next level
		if (result == questgiver_status::None && hasQuestAvailableNextLevel)
		{
			result = questgiver_status::Unavailable;
		}

		return result;
	}

	bool GameCreatureS::ProvidesQuest(uint32 questId) const
	{
		return std::find_if(GetEntry().quests().begin(), GetEntry().quests().end(), [questId](uint32 id) { return id == questId; }) != GetEntry().quests().end();
	}

	bool GameCreatureS::EndsQuest(uint32 questId) const
	{
		return std::find_if(GetEntry().end_quests().begin(), GetEntry().end_quests().end(), [questId](uint32 id) { return id == questId; }) != GetEntry().end_quests().end();
	}

	void GameCreatureS::RaiseTrigger(trigger_event::Type e, GameUnitS* triggeringUnit)
	{
		for (const auto& triggerId : GetEntry().triggers())
		{
			if (const auto* triggerEntry = GetProject().triggers.getById(triggerId))
			{
				for (const auto& triggerEvent : triggerEntry->newevents())
				{
					if (triggerEvent.type() == e)
					{
						unitTrigger(std::cref(*triggerEntry), std::ref(*this), triggeringUnit);
					}
				}
			}
		}
	}

	void GameCreatureS::RaiseTrigger(trigger_event::Type e, const std::vector<uint32>& data, GameUnitS* triggeringUnit)
	{
		for (const auto& triggerId : GetEntry().triggers())
		{
			const auto* triggerEntry = GetProject().triggers.getById(triggerId);
			if (!triggerEntry)
			{
				continue;
			}

			for (const auto& triggerEvent : triggerEntry->newevents())
			{
				if (triggerEvent.type() != e)
				{
					continue;
				}

				if (e == trigger_event::OnGossipAction)
				{
					const uint32 menuId = triggerEvent.data_size() > 0 ? triggerEvent.data(0) : 0;
					const uint32 actionId = triggerEvent.data_size() > 1 ? triggerEvent.data(1) : 0;

					if (data.size() < 2 || data[0] != menuId || data[1] != actionId)
					{
						continue;
					}
				}
				else if (triggerEvent.data_size() > 0)
				{
					switch (e)
					{
					case trigger_event::OnSpellHit:
					case trigger_event::OnSpellAuraRemoved:
					case trigger_event::OnEmote:
					case trigger_event::OnSpellCast:
						if (triggerEvent.data(0) != 0)
						{
							if (data.empty() || data[0] != triggerEvent.data(0))
								continue;
						}
						break;
					default:
						break;
					}
				}

				unitTrigger(std::cref(*triggerEntry), std::ref(*this), triggeringUnit);
			}
		}
	}

	void GameCreatureS::AddCombatParticipant(const GameUnitS& unit)
	{
		m_combatParticipantGuids.insert(unit.GetGuid());
	}

	void GameCreatureS::RemoveCombatParticipant(const uint64 unitGuid)
	{
		m_combatParticipantGuids.erase(unitGuid);
	}

	void GameCreatureS::SetMovementType(CreatureMovement movementType)
	{
		if (m_movement != movementType)
		{
			m_movement = movementType;
			m_ai->OnCreatureMovementChanged();
		}
	}
	void GameCreatureS::RefreshStats()
	{
		GameUnitS::RefreshStats();

		// Check if this creature uses the new stat-based system
		if (m_entry->usestatbasedsystem())
		{
			CalculateStatBasedStats();
		}
		else
		{
			// Legacy stat calculation
			Set<uint32>(object_fields::Armor, m_entry->armor());
			Set<float>(object_fields::MinDamage, m_entry->minmeleedmg());
			Set<float>(object_fields::MaxDamage, m_entry->maxmeleedmg());
		}
	}

	void GameCreatureS::CalculateStatBasedStats()
	{
		// Get the unit class entry if specified
		const proto::UnitClassEntry* unitClass = nullptr;
		if (m_entry->unitclassid() != 0)
		{
			unitClass = GetProject().unitClasses.getById(m_entry->unitclassid());
		}

		ASSERT(unitClass);
		ASSERT(unitClass->levelbasevalues_size() > 0);

		const int32 level = std::min(static_cast<int32>(GetLevel()), unitClass->levelbasevalues_size());
		ASSERT(level > 0);

		const float eliteMultiplier = m_entry->elitestatmultiplier();

		// Calculate base stats with level scaling
		const auto& base = unitClass->levelbasevalues(level - 1);
		const uint32 baseStamina = static_cast<uint32>(base.stamina() * eliteMultiplier);
		const uint32 baseStrength = static_cast<uint32>(base.stamina() * eliteMultiplier);
		const uint32 baseAgility = static_cast<uint32>(base.stamina() * eliteMultiplier);
		const uint32 baseIntellect = static_cast<uint32>(base.intellect() * eliteMultiplier);
		const uint32 baseSpirit = static_cast<uint32>(base.spirit() * eliteMultiplier);
		SetModifierValue(GetUnitModByStat(0), unit_mod_type::BaseValue, baseStamina);
		SetModifierValue(GetUnitModByStat(1), unit_mod_type::BaseValue, baseStrength);
		SetModifierValue(GetUnitModByStat(2), unit_mod_type::BaseValue, baseAgility);
		SetModifierValue(GetUnitModByStat(3), unit_mod_type::BaseValue, baseIntellect);
		SetModifierValue(GetUnitModByStat(4), unit_mod_type::BaseValue, baseSpirit);

		// Apply the base stats
		const float totalStamina = GetCalculatedModifierValue(unit_mods::StatStamina);
		const float totalStrength = GetCalculatedModifierValue(unit_mods::StatStrength);
		const float totalAgility = GetCalculatedModifierValue(unit_mods::StatAgility);
		const float totalIntellect = GetCalculatedModifierValue(unit_mods::StatIntellect);
		const float totalSpirit = GetCalculatedModifierValue(unit_mods::StatSpirit);
		Set<uint32>(object_fields::StatStamina, static_cast<uint32>(totalStamina));
		Set<uint32>(object_fields::StatStrength, static_cast<uint32>(totalStrength));
		Set<uint32>(object_fields::StatAgility, static_cast<uint32>(totalAgility));
		Set<uint32>(object_fields::StatIntellect, static_cast<uint32>(totalIntellect));
		Set<uint32>(object_fields::StatSpirit, static_cast<uint32>(totalSpirit));

		// Calculate health (base + per level + stamina modifier)
		uint32 baseHealth = static_cast<uint32>(base.health() * eliteMultiplier);
		uint32 healthFromStats = 0;

		// Add health from unit class stat sources
		if (unitClass != nullptr)
		{
			for (const auto& healthSource : unitClass->healthstatsources())
			{
				switch (healthSource.statid())
				{
					case 2: // STAMINA
						healthFromStats += static_cast<uint32>((std::max<int32>(totalStamina, 20) - 20) * healthSource.factor());
						break;
					case 0: // STRENGTH
						healthFromStats += static_cast<uint32>((std::max<int32>(totalStrength, 20U) - 20) * healthSource.factor());
						break;
					case 1: // AGILITY
						healthFromStats += static_cast<uint32>((std::max<int32>(totalAgility, 20U) - 20) * healthSource.factor());
						break;
					case 3: // INTELLECT
						healthFromStats += static_cast<uint32>((std::max<int32>(totalIntellect, 20U) - 20) * healthSource.factor());
						break;
					case 4: // SPIRIT
						healthFromStats += static_cast<uint32>((std::max<int32>(totalSpirit, 20U) - 20) * healthSource.factor());
						break;
				}
			}
		}

		const uint32 finalHealth = baseHealth + healthFromStats;
		Set<uint32>(object_fields::MaxHealth, finalHealth);

		// Calculate mana (base + per level + intellect modifier)
		uint32 baseMana = static_cast<uint32>(base.mana() * eliteMultiplier);
		uint32 manaFromStats = 0;

		// Add mana from unit class stat sources
		if (unitClass != nullptr)
		{
			for (const auto& manaSource : unitClass->manastatsources())
			{
				switch (manaSource.statid())
				{
					case 2: // STAMINA
						manaFromStats += static_cast<uint32>((std::max<int32>(totalStamina, 20) - 20) * manaSource.factor());
						break;
					case 0: // STRENGTH
						manaFromStats += static_cast<uint32>((std::max<int32>(totalStrength, 20) - 20) * manaSource.factor());
						break;
					case 1: // AGILITY
						manaFromStats += static_cast<uint32>((std::max<int32>(totalAgility, 20) - 20) * manaSource.factor());
						break;
					case 3: // INTELLECT
						manaFromStats += static_cast<uint32>((std::max<int32>(totalIntellect, 20) - 20) * manaSource.factor());
						break;
					case 4: // SPIRIT
						manaFromStats += static_cast<uint32>((std::max<int32>(totalSpirit, 20) - 20) * manaSource.factor());
						break;
				}
			}
		}

		const uint32 finalMana = baseMana + manaFromStats;
		Set<uint32>(object_fields::MaxMana, finalMana);

		// Calculate armor (base + per level + agility modifier)
		uint32 baseArmor = static_cast<uint32>((m_entry->basearmor() + m_entry->armorperlevel() * (level - 1)) * eliteMultiplier);
		uint32 armorFromStats = 0;

		// Add armor from unit class stat sources
		if (unitClass != nullptr)
		{
			for (const auto& armorSource : unitClass->armorstatsources())
			{
				switch (armorSource.statid())
				{
					case 2: // STAMINA
						armorFromStats += static_cast<uint32>((std::max<int32>(totalStamina, 20) - 20) * armorSource.factor());
						break;
					case 0: // STRENGTH
						armorFromStats += static_cast<uint32>((std::max<int32>(totalStrength, 20) - 20) * armorSource.factor());
						break;
					case 1: // AGILITY
						armorFromStats += static_cast<uint32>((std::max<int32>(totalAgility, 20) - 20) * armorSource.factor());
						break;
					case 3: // INTELLECT
						armorFromStats += static_cast<uint32>((std::max<int32>(totalIntellect, 20) - 20) * armorSource.factor());
						break;
					case 4: // SPIRIT
						armorFromStats += static_cast<uint32>((std::max<int32>(totalSpirit, 20) - 20) * armorSource.factor());
						break;
				}
			}
		}

		const uint32 finalArmor = baseArmor + armorFromStats;
		Set<uint32>(object_fields::Armor, finalArmor);

		// Calculate attack power
		uint32 attackPower = 0;
		if (unitClass != nullptr)
		{
			attackPower = static_cast<uint32>((unitClass->attackpoweroffset() + unitClass->attackpowerperlevel() * (level - 1)) * eliteMultiplier);

			// Add attack power from unit class stat sources
			for (const auto& attackPowerSource : unitClass->attackpowerstatsources())
			{
				switch (attackPowerSource.statid())
				{
					case 2: // STAMINA
						attackPower += static_cast<uint32>((std::max<int32>(totalStamina, 20) - 20) * attackPowerSource.factor());
						break;
					case 0: // STRENGTH
						attackPower += static_cast<uint32>((std::max<int32>(totalStrength, 20) - 20) * attackPowerSource.factor());
						break;
					case 1: // AGILITY
						attackPower += static_cast<uint32>((std::max<int32>(totalAgility, 20) - 20) * attackPowerSource.factor());
						break;
					case 3: // INTELLECT
						attackPower += static_cast<uint32>((std::max<int32>(totalIntellect, 20) - 20) * attackPowerSource.factor());
						break;
					case 4: // SPIRIT
						attackPower += static_cast<uint32>((std::max<int32>(totalSpirit, 20) - 20) * attackPowerSource.factor());
						break;
				}
			}
		}

		Set<uint32>(object_fields::AttackPower, attackPower);

		// Calculate damage (base damage from legacy system + scaling + variance)
		float baseDamage = static_cast<float>((m_entry->minmeleedmg() + m_entry->damageperlevel() * (level - 1)) * eliteMultiplier);
		const float damageVariance = m_entry->basedamagevariance();
		
		// Calculate damage based on attack power and weapon speed if unit class defines it
		if (unitClass != nullptr && attackPower > 0)
		{
			const uint32 attackTime = unitClass->basemeleeattacktime();
			const float weaponSpeed = static_cast<float>(attackTime) / 1000.0f;
			baseDamage = std::max(baseDamage, static_cast<float>(attackPower) * weaponSpeed / 14.0f);
		}

		const float minDamage = baseDamage * (1.0f - damageVariance);
		const float maxDamage = baseDamage * (1.0f + damageVariance);

		Set<float>(object_fields::MinDamage, minDamage);
		Set<float>(object_fields::MaxDamage, maxDamage);		// Set power type from unit class
		if (unitClass != nullptr)
		{
			// Convert protobuf power type to game power type (they match for the basic types)
			uint32 gamePowerType = power_type::Mana; // Default
			switch (unitClass->powertype())
			{
				case 0: // MANA
					gamePowerType = power_type::Mana;
					break;
				case 1: // RAGE
					gamePowerType = power_type::Rage;
					break;
				case 2: // ENERGY
					gamePowerType = power_type::Energy;
					break;
				default:
					gamePowerType = power_type::Mana;
					break;
			}

			Set<uint32>(object_fields::PowerType, gamePowerType);
		}
	}

	const String& GameCreatureS::GetName() const
	{
		return m_entry ? m_entry->name() : m_originalEntry.name();
	}
}
