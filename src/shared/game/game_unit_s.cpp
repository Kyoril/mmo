// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "game_unit_s.h"

namespace mmo
{
	GameUnitS::GameUnitS(const proto::Project& project, TimerQueue& timers): GameObjectS(project)
	                                                                         , m_timers(timers)
	                                                                         , m_despawnCountdown(timers)
	{
		// Setup unit mover
		m_mover = make_unique<UnitMover>(*this);
	}

	void GameUnitS::Initialize()
	{
		GameObjectS::Initialize();

		// Initialize some values
		Set(object_fields::Type, ObjectTypeId::Unit);
		Set(object_fields::Scale, 1.0f);

		// Set unit values
		Set(object_fields::Health, 60u);
		Set(object_fields::MaxHealth, 60u);

		Set(object_fields::Mana, 100);
		Set(object_fields::Rage, 0);	
		Set(object_fields::Energy, 100);

		Set(object_fields::MaxMana, 100);
		Set(object_fields::MaxRage, 1000);
		Set(object_fields::MaxEnergy, 100);
	}

	void GameUnitS::TriggerDespawnTimer(GameTime despawnDelay)
	{
		m_despawnCountdown.SetEnd(GetAsyncTimeMs() + despawnDelay);
	}

	void GameUnitS::OnDespawnTimer()
	{
		if (m_worldInstance)
		{
			m_worldInstance->RemoveGameObject(*this);
		}
	}
}
