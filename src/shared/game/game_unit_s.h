// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "game_object_s.h"
#include "unit_mover.h"

namespace mmo
{
	/// @brief Represents a living object (unit) in the game world.
	class GameUnitS : public GameObjectS
	{
	public:

		/// Fired when this unit was killed. Parameter: GameUnit* killer (may be nullptr if killer
		/// information is not available (for example due to environmental damage))
		signal<void(GameUnitS*)> killed;
		signal<void(GameUnitS&, float)> threatened;
		signal<void(GameUnitS*, uint32)> takenDamage;

	public:
		GameUnitS(const proto::Project& project,
			TimerQueue& timers);

		virtual ~GameUnitS() override = default;

		virtual void Initialize() override;

		void TriggerDespawnTimer(GameTime despawnDelay);

	protected:
		virtual void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::UnitFieldCount);
		}

	private:

		/// 
		void OnDespawnTimer();

	public:
		TimerQueue& GetTimers() const { return m_timers; }

		UnitMover& GetMover() const { return *m_mover; }

	protected:
		TimerQueue& m_timers;
		Countdown m_despawnCountdown;
		std::unique_ptr<UnitMover> m_mover;
	};
}
