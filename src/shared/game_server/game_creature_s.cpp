
#include "game_creature_s.h"
#include "proto_data/project.h"
#include "world_instance.h"
#include "binary_io/vector_sink.h"

namespace mmo
{
	GameCreatureS::GameCreatureS(
		const proto::Project& project,
		TimerQueue& timers,
		const proto::UnitEntry& entry)
		: GameUnitS(project, timers)
		, m_originalEntry(entry)
		, m_entry(&m_originalEntry)
	{
	}

	void GameCreatureS::Initialize()
	{
		GameUnitS::Initialize();

		ASSERT(m_entry);

		// Initialize creature based on unit entry values
		Set<uint32>(object_fields::Level, m_entry->minlevel());
		SetEntry(*m_entry);
		Set<uint32>(object_fields::Health, m_entry->minlevelhealth());
		Set<uint32>(object_fields::Mana, m_entry->minlevelmana());
		ClearFieldChanges();

		// Setup AI
		m_ai = make_unique<CreatureAI>(*this, CreatureAI::Home(m_movementInfo.position));
	}

	void GameCreatureS::SetEntry(const proto::UnitEntry& entry)
	{
		// Setup new entry
		m_entry = &entry;

		Set<uint32>(object_fields::MaxHealth, m_entry->minlevelhealth());
		Set<uint32>(object_fields::MaxMana, m_entry->minlevelmana());
		Set<uint32>(object_fields::Entry, m_entry->id());
		Set<float>(object_fields::Scale, m_entry->scale());
		Set<uint32>(object_fields::DisplayId, m_entry->malemodel());	// TODO: gender roll
		Set<uint32>(object_fields::FactionTemplate, m_entry->factiontemplate());
		Set<uint32>(object_fields::PowerType, power_type::Mana);	// TODO
		RefreshStats();
	}

	void GameCreatureS::AddLootRecipient(uint64 guid)
	{
		m_lootRecipients.add(guid);
	}

	void GameCreatureS::RemoveLootRecipients()
	{
		m_lootRecipients.clear();
	}

	bool GameCreatureS::IsLootRecipient(GamePlayerS& character) const
	{
		return m_lootRecipients.contains(character.GetGuid());
	}

	void GameCreatureS::SetUnitLoot(std::unique_ptr<LootInstance> unitLoot)
	{
		m_unitLoot = std::move(unitLoot);
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

		Set<uint32>(object_fields::Armor, m_entry->armor());
		Set<float>(object_fields::MinDamage, m_entry->minmeleedmg());
		Set<float>(object_fields::MaxDamage, m_entry->maxmeleedmg());
	}
}
