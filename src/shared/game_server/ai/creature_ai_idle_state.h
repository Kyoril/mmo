// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "creature_ai_state.h"
#include "game_server/world/unit_watcher.h"
#include "base/countdown.h"
#include <cstddef>
#include <vector>

namespace mmo
{
	class GameObjectS;

	/// Handle the idle state of a creature AI. In this state, most units
	/// watch for hostile units which come close enough, and start attacking these
	/// units.
	class CreatureAIIdleState : public CreatureAIState
	{
	public:
		/// Initializes a new instance of the CreatureAIIdleState class.
		/// @param ai The ai class instance this state belongs to.
		explicit CreatureAIIdleState(CreatureAI& ai);
		/// Default destructor.
		virtual ~CreatureAIIdleState();

	public:
		/// @copydoc CreatureAIState::OnEnter
		virtual void OnEnter() override;

		/// @copydoc CreatureAIState::OnLeave
		virtual void OnLeave() override;

		/// @copydoc CreatureAIState::OnCreatureMovementChanged
		virtual void OnCreatureMovementChanged() override;

		/// @copydoc CreatureAIState::OnControlledMoved
		virtual void OnControlledMoved() override;

		virtual void OnDamage(GameUnitS& attacker) override;

	protected:
		/// @brief Advances the creature's current idle movement mode.
		void AdvanceIdleMovement();

		/// @brief Starts moving towards the next patrol waypoint.
		void MoveToNextPatrolWaypoint();

		/// @brief Chooses the best patrol waypoint to resume from.
		/// @return Zero-based patrol waypoint index.
		[[nodiscard]] size_t FindClosestPatrolWaypointIndex() const;

		/// @brief Starts a wait countdown in milliseconds.
		/// @param waitTimeMs Delay before the next idle movement step.
		void StartWait(uint32 waitTimeMs);

		void OnWaitCountdownExpired();

		void OnTargetReached();

		void MoveToRandomPointInRange();

	private:

		Countdown m_waitCountdown;

		scoped_connection_container m_connections;

		std::unique_ptr<UnitWatcher> m_unitWatcher;
		size_t m_nextPatrolWaypointIndex = 0;
		/// Ordered patrol waypoint indices of the chain currently being traversed.
		std::vector<size_t> m_currentChainWaypointIndices;
		/// Current progress through the chain — the index into m_currentChainWaypointIndices
		/// representing the waypoint the creature is currently heading toward.
		size_t m_chainProgressIndex = 0;
	};
}
