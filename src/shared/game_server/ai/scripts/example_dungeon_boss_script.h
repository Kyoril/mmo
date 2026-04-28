// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_server/ai/creature_combat_script.h"

namespace mmo
{
	/**
	 * @brief Example dungeon boss combat script demonstrating multi-phase encounters.
	 *
	 * This is a reference implementation of a 5-player dungeon boss designed for
	 * level 10 players. It demonstrates the full capability of the combat script
	 * framework:
	 *
	 * **Phase 1 (100% - 50% HP): "The Warlord"**
	 * - Standard melee combat with periodic cleave attacks
	 * - Periodically charges the furthest player
	 * - Yells taunts during combat
	 *
	 * **Transition (at 50% HP): "Transformation"**
	 * - Boss becomes immune and un-attackable
	 * - Stops attacking and moving
	 * - Yells transformation text
	 * - After a delay, transforms (equipment change), becomes attackable again
	 * - Enters Phase 2
	 *
	 * **Phase 2 (50% - 0% HP): "The Berserker"**
	 * - Increased attack speed (through spells/auras)
	 * - Periodic ground slam (damage to all nearby players)
	 * - Charges random players more frequently
	 * - Enrage at 10% HP
	 *
	 * To use this script:
	 * 1. Create the creature entry and configure spells in your data
	 * 2. Register the script in your server startup code:
	 *    @code
	 *    #include "scripts/example_dungeon_boss_script.h"
	 *    #include "creature_combat_script_registry.h"
	 *    // ...
	 *    CreatureCombatScriptRegistry::Instance()
	 *        .RegisterScript<ExampleDungeonBossScript>(YOUR_BOSS_ENTRY_ID);
	 *    @endcode
	 *
	 * **Spell IDs** are placeholders. Replace them with actual spell entry IDs
	 * from your data when setting up the boss encounter.
	 */
	class ExampleDungeonBossScript final : public CreatureCombatScript
	{
	public:

		/**
		 * @brief Constructs the dungeon boss script.
		 * @param combatState The combat state that owns this script.
		 */
		explicit ExampleDungeonBossScript(CreatureAICombatState& combatState);

		/**
		 * @brief Destructor.
		 */
		~ExampleDungeonBossScript() override;

	public:

		/// @copydoc CreatureCombatScript::OnCombatStart
		void OnCombatStart() override;

		/// @copydoc CreatureCombatScript::OnCombatEnd
		void OnCombatEnd() override;

		/// @copydoc CreatureCombatScript::OnChooseAction
		bool OnChooseAction() override;

		/// @copydoc CreatureCombatScript::OnHealthThresholdReached
		void OnHealthThresholdReached(uint8 percentage) override;

		/// @copydoc CreatureCombatScript::OnTimerExpired
		void OnTimerExpired(uint32 timerId) override;

		/// @copydoc CreatureCombatScript::OnPhaseChanged
		void OnPhaseChanged(uint32 oldPhase, uint32 newPhase) override;

		/// @copydoc CreatureCombatScript::CanDie
		bool CanDie() const override;

		/// @copydoc CreatureCombatScript::CanMove
		bool CanMove() const override;

	private:

		/// Executes Phase 1 combat logic.
		void DoPhase1Action();

		/// Executes Phase 2 combat logic.
		void DoPhase2Action();

		/// Charges to the furthest or a random player.
		void DoChargeAttack();

	private:

		// === Combat Phases ===

		/// Phase 1: Standard melee combat with periodic abilities.
		static constexpr uint32 PHASE_WARLORD = 0;

		/// Transition phase: Boss is transforming (immune).
		static constexpr uint32 PHASE_TRANSITION = 1;

		/// Phase 2: Enraged berserker mode.
		static constexpr uint32 PHASE_BERSERKER = 2;

		// === Timer IDs ===

		/// Periodic cleave / special attack timer (Phase 1).
		static constexpr uint32 TIMER_CLEAVE = 1;

		/// Charge attack timer.
		static constexpr uint32 TIMER_CHARGE = 2;

		/// Transformation delay timer.
		static constexpr uint32 TIMER_TRANSFORM_COMPLETE = 3;

		/// Ground slam timer (Phase 2).
		static constexpr uint32 TIMER_GROUND_SLAM = 4;

		/// Periodic taunt/yell timer.
		static constexpr uint32 TIMER_TAUNT = 5;

		/// Enrage timer (Phase 2 at low HP).
		static constexpr uint32 TIMER_ENRAGE = 6;

		// === Spell IDs (placeholders - replace with actual spell data IDs) ===

		/// Cleave: Hits the current target and nearby enemies.
		/// Replace with your actual cleave spell ID.
		static constexpr uint32 SPELL_CLEAVE = 0;

		/// Charge: Rushes to a distant target.
		/// Replace with your actual charge spell ID.
		static constexpr uint32 SPELL_CHARGE = 0;

		/// Ground Slam: AoE damage around the boss.
		/// Replace with your actual ground slam spell ID.
		static constexpr uint32 SPELL_GROUND_SLAM = 0;

		/// Enrage: Increases damage and attack speed.
		/// Replace with your actual enrage spell ID.
		static constexpr uint32 SPELL_ENRAGE = 0;

		// === Timing Constants (milliseconds) ===

		/// Interval between cleave attacks in Phase 1.
		static constexpr uint32 CLEAVE_INTERVAL_MS = 8000;

		/// Interval between charge attacks in Phase 1.
		static constexpr uint32 CHARGE_INTERVAL_PHASE1_MS = 15000;

		/// Interval between charge attacks in Phase 2 (more frequent).
		static constexpr uint32 CHARGE_INTERVAL_PHASE2_MS = 10000;

		/// Duration of the transformation sequence.
		static constexpr uint32 TRANSFORM_DURATION_MS = 5000;

		/// Interval between ground slams in Phase 2.
		static constexpr uint32 GROUND_SLAM_INTERVAL_MS = 12000;

		/// Interval between boss taunts.
		static constexpr uint32 TAUNT_INTERVAL_MS = 20000;

		// === State ===

		/// Whether the boss is currently in the transition (immune/transforming).
		bool m_isTransitioning;

		/// Whether the enrage buff has been applied.
		bool m_hasEnraged;
	};
}
