
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
		, m_entry(nullptr)
	{
	}

	void GameCreatureS::Initialize()
	{
		GameUnitS::Initialize();

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
