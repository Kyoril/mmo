// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/ai/creature_ai_state.h"
#include "base/countdown.h"
#include "base/signal.h"
#include "math/vector3.h"

namespace mmo
{
	/// The out-of-combat state of a pet (a creature AI in pet mode). Every PetUpdateInterval it
	/// looks for something to fight - what the owner attacks, what attacks the owner, what attacks
	/// the pet, as the pet's strategy allows (see ChoosePetTarget) - and otherwise keeps to its
	/// follow slot beside the owner, or to its stay position. It never pulls on its own: there is
	/// no unit watcher. A pet too far from its owner, or without a path to it, teleports to its side.
	class CreatureAIPetState final : public CreatureAIState
	{
	public:
		/// Initializes a new instance of the CreatureAIPetState class.
		/// @param ai The ai class instance this state belongs to (in pet mode).
		explicit CreatureAIPetState(CreatureAI& ai);

		~CreatureAIPetState() override;

	public:
		/// @copydoc CreatureAIState::OnEnter
		void OnEnter() override;

		/// @copydoc CreatureAIState::OnLeave
		void OnLeave() override;

		/// @copydoc CreatureAIState::OnDamage
		void OnDamage(GameUnitS& attacker) override;

		/// Runs one engage / follow step right away and schedules the next one. May leave the
		/// state (entering combat).
		void Update();

	private:
		/// Looks for something to fight and engages it.
		/// @param owner The pet's owner.
		/// @return True if the pet entered combat (this state is no longer active).
		bool TryEngage(GameUnitS& owner);

		/// Moves the pet towards a position, teleporting it there if there is no path.
		/// @param destination Where to go.
		/// @param facing Facing to use when teleporting.
		/// @param run Whether to run.
		void MoveTowards(const Vector3& destination, const Radian& facing, bool run);

		/// Makes the pet at least as fast as its owner.
		/// @param owner The pet's owner.
		void MatchOwnerSpeed(const GameUnitS& owner);

	private:
		Countdown m_updateCountdown;
		scoped_connection_container m_connections;

		/// Last destination given to the mover, to avoid replanning every poll while the owner stands still.
		Vector3 m_lastDestination;
		bool m_hasDestination = false;
	};
}
