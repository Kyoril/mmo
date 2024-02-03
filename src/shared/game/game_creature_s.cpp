
#include "game_creature_s.h"
#include "proto_data/project.h"
#include "world_instance.h"
#include "each_tile_in_sight.h"
#include "binary_io/vector_sink.h"
#include "log/default_log_levels.h"

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

		// Initialize creature based on unit entry values
		Set<uint32>(object_fields::Level, m_entry->minlevel(), false);
		Set<uint32>(object_fields::MaxHealth, m_entry->minlevelhealth(), false);
		Set<uint32>(object_fields::Health, m_entry->minlevelhealth(), false);
		Set<uint32>(object_fields::MaxMana, m_entry->minlevelmana(), false);
		Set<uint32>(object_fields::Mana, m_entry->minlevelmana(), false);
		Set<uint32>(object_fields::Entry, m_entry->id(), false);
		Set<float>(object_fields::Scale, m_entry->scale(), false);
		ClearFieldChanges();

		// Setup AI
		m_ai = make_unique<CreatureAI>(
			*this, CreatureAI::Home(m_movementInfo.position));
	}

	void GameCreatureS::SetEntry(const proto::UnitEntry& entry)
	{
		// Setup new entry
		m_entry = &entry;
	}
}
