// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/ai/creature_combat_script.h"

namespace mmo
{
	/**
	 * @brief Combat script for training dummy NPCs.
	 *
	 * A training dummy:
	 * - Cannot move (stays at its spawn position)
	 * - Cannot die (health is restored if it would die)
	 * - Does not fight back (no attacks, no spells)
	 * - Resets combat after not receiving damage for a configurable timeout
	 * - Does not walk back on reset (it never moved)
	 *
	 * This serves as both a functional NPC type and a reference implementation
	 * for how to create simple combat scripts.
	 */
	class TrainingDummyCombatScript final : public CreatureCombatScript
	{
	public:

		/**
		 * @brief Constructs a training dummy combat script.
		 * @param combatState The combat state that owns this script.
		 */
		explicit TrainingDummyCombatScript(CreatureAICombatState& combatState);

		/**
		 * @brief Destructor.
		 */
		~TrainingDummyCombatScript() override;

	public:

		/// @copydoc CreatureCombatScript::OnCombatStart
		void OnCombatStart() override;

		/// @copydoc CreatureCombatScript::OnChooseAction
		bool OnChooseAction() override;

		/// @copydoc CreatureCombatScript::OnDamageTaken
		void OnDamageTaken(GameUnitS& attacker) override;

		/// @copydoc CreatureCombatScript::CanDie
		bool CanDie() const override;

		/// @copydoc CreatureCombatScript::CanMove
		bool CanMove() const override;

		/// @copydoc CreatureCombatScript::ShouldAutoAttack
		bool ShouldAutoAttack() const override;

		/// @copydoc CreatureCombatScript::ShouldResetCombat
		bool ShouldResetCombat() const override;

	private:

		/// Timer ID for the reset-after-no-damage timeout.
		static constexpr uint32 TIMER_RESET_CHECK = 1;

		/// How long (ms) without damage before the dummy resets combat.
		static constexpr uint32 RESET_TIMEOUT_MS = 5000;

		/// Timestamp of the last damage received.
		GameTime m_lastDamageTime;
	};
}
