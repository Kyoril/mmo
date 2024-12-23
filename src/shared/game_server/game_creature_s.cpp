
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
		, m_entry(nullptr)
	{
	}

	void GameCreatureS::Initialize()
	{
		GameUnitS::Initialize();

		// Initialize creature based on unit entry values
		Set<uint32>(object_fields::Level, m_originalEntry.minlevel());
		ClearFieldChanges();

		// Setup AI
		m_ai = make_unique<CreatureAI>(*this, CreatureAI::Home(m_movementInfo.position));
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
		uint32 npcFlags = m_entry->npcflags();
		if (m_entry->trainerentry())
		{
			npcFlags |= npc_flags::Trainer;
		}
		if (m_entry->vendorentry())
		{
			npcFlags |= npc_flags::Vendor;
		}

		if ((npcFlags & (npc_flags::Trainer | npc_flags::Vendor)) != 0)
		{
			npcFlags |= npc_flags::Gossip;
		}

		Set<uint32>(object_fields::NpcFlags, npcFlags);
		Set<uint32>(object_fields::MaxHealth, m_entry->minlevelhealth());
		Set<uint32>(object_fields::MaxMana, m_entry->minlevelmana());
		Set<uint32>(object_fields::Entry, m_entry->id());
		Set<float>(object_fields::Scale, m_entry->scale());
		Set<uint32>(object_fields::DisplayId, m_entry->malemodel());	// TODO: gender roll
		Set<uint32>(object_fields::FactionTemplate, m_entry->factiontemplate());
		Set<uint32>(object_fields::PowerType, power_type::Mana);	// TODO
		RefreshStats();

		if (firstInitialization)
		{
			Set<uint32>(object_fields::Health, m_entry->minlevelhealth());
			Set<uint32>(object_fields::Mana, m_entry->minlevelmana());
			ClearFieldChanges();
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

	bool GameCreatureS::IsLootRecipient(GamePlayerS& character) const
	{
		return m_lootRecipients.contains(character.GetGuid());
	}

	void GameCreatureS::SetUnitLoot(std::unique_ptr<LootInstance> unitLoot)
	{
		m_unitLoot = std::move(unitLoot);

		// This unit is lootable if the unit loot is set
		if (m_unitLoot)
		{
			AddFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
		}
		else
		{
			RemoveFlag<uint32>(object_fields::Flags, unit_flags::Lootable);
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

		Set<uint32>(object_fields::Armor, m_entry->armor());
		Set<float>(object_fields::MinDamage, m_entry->minmeleedmg());
		Set<float>(object_fields::MaxDamage, m_entry->maxmeleedmg());
	}
}
