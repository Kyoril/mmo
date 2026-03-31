// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/countdown.h"
#include "math/vector3.h"

#include <set>
#include <map>
#include <vector>
#include <memory>
#include <functional>

namespace mmo
{
	class CreatureAICombatState;
	class GameCreatureS;
	class GameUnitS;

	namespace proto
	{
		class SpellEntry;
		class Project;
	}

	/**
	 * @brief Abstract base class for creature combat scripts.
	 *
	 * Provides a framework for implementing custom creature combat behaviors,
	 * including boss fights with multiple phases, special targeting logic,
	 * timed events, and health-based transitions.
	 *
	 * To create a custom combat script:
	 * 1. Derive from this class
	 * 2. Override the desired virtual hooks
	 * 3. Register the script with CreatureCombatScriptRegistry
	 *
	 * The script has access to actions (casting spells, moving, chatting),
	 * target selection helpers (furthest player, random player, etc.),
	 * timer management, phase management, and health threshold tracking.
	 *
	 * If OnChooseAction() returns true, the default AI logic is skipped for
	 * that tick. Return false to let the standard melee/caster AI handle it.
	 */
	class CreatureCombatScript
	{
		/// Allow the combat state to call protected methods like CheckHealthThresholds.
		friend class CreatureAICombatState;

		/// Allow GameCreatureS to call SetPhase for trigger action forwarding.
		friend class GameCreatureS;

	public:

		/**
		 * @brief Constructs a combat script attached to a combat state.
		 * @param combatState The combat state that owns this script.
		 */
		explicit CreatureCombatScript(CreatureAICombatState& combatState);

		/**
		 * @brief Virtual destructor.
		 */
		virtual ~CreatureCombatScript();

	public:

		// =====================================================================
		// Combat Lifecycle Hooks
		// =====================================================================

		/**
		 * @brief Called when this creature enters combat.
		 *
		 * Override this to set up initial phase, register health thresholds,
		 * schedule timers, etc.
		 */
		virtual void OnCombatStart();

		/**
		 * @brief Called when this creature leaves combat (reset or death).
		 *
		 * Override this to clean up any custom state.
		 */
		virtual void OnCombatEnd();

		// =====================================================================
		// Main AI Decision Hook
		// =====================================================================

		/**
		 * @brief Called each AI tick to decide the next action.
		 * @return True if the script handled the action (skips default AI logic).
		 *         False to let the default melee/caster/ranged AI run.
		 *
		 * Override this to implement fully custom combat behavior per phase.
		 * For simple overrides, you can handle specific phases and return
		 * false for phases that should use default AI.
		 */
		virtual bool OnChooseAction();

		// =====================================================================
		// Event Hooks
		// =====================================================================

		/**
		 * @brief Called when the creature takes damage.
		 * @param attacker The unit that dealt damage.
		 *
		 * Called after threat is applied. Health thresholds are checked
		 * automatically after this call.
		 */
		virtual void OnDamageTaken(GameUnitS& attacker);

		/**
		 * @brief Called when a spell cast ends.
		 * @param succeeded Whether the cast completed successfully.
		 */
		virtual void OnSpellCastEnded(bool succeeded);

		/**
		 * @brief Called when a registered health threshold is crossed.
		 * @param percentage The health percentage that was reached (0-100).
		 *
		 * Each threshold fires exactly once per combat. Register thresholds
		 * during OnCombatStart() using RegisterHealthThreshold().
		 */
		virtual void OnHealthThresholdReached(uint8 percentage);

		/**
		 * @brief Called when a scheduled timer expires.
		 * @param timerId The ID of the expired timer.
		 *
		 * Schedule timers using ScheduleTimer().
		 */
		virtual void OnTimerExpired(uint32 timerId);

		/**
		 * @brief Called when the combat phase changes.
		 * @param oldPhase The previous phase number.
		 * @param newPhase The new phase number.
		 *
		 * Triggered by SetPhase(). Override to set up phase-specific
		 * spells, timers, equipment, etc.
		 */
		virtual void OnPhaseChanged(uint32 oldPhase, uint32 newPhase);

		/**
		 * @brief Called when a unit on the threat list dies.
		 * @param target The unit that died.
		 */
		virtual void OnTargetDied(GameUnitS& target);

		// =====================================================================
		// Behavior Overrides
		// =====================================================================

		/**
		 * @brief Whether this creature can be killed.
		 * @return True if the creature can die (default: true).
		 *
		 * Return false for training dummies or bosses during invulnerability phases.
		 */
		virtual bool CanDie() const;

		/**
		 * @brief Whether this creature can move.
		 * @return True if the creature can move (default: true).
		 *
		 * Return false for training dummies or stationary creatures.
		 */
		virtual bool CanMove() const;

		/**
		 * @brief Whether this creature should auto-attack its victim.
		 * @return True if auto-attack should be active (default: true).
		 *
		 * Return false for training dummies or creatures that should not
		 * swing at their target. The creature will still track its victim
		 * for threat purposes but will not initiate attack swings.
		 */
		virtual bool ShouldAutoAttack() const;

		/**
		 * @brief Whether this creature should reset combat.
		 * @return True if combat should be reset.
		 *
		 * Called by the combat state's reset logic. Override to implement
		 * custom reset conditions (e.g., training dummy resets after no
		 * damage for X seconds).
		 */
		virtual bool ShouldResetCombat() const;

		/**
		 * @brief Override spell target selection.
		 * @param spell The spell being cast.
		 * @return A target unit, or nullptr to use default targeting.
		 *
		 * Return nullptr to fall through to the default priority-based
		 * target selection.
		 */
		virtual GameUnitS* SelectSpellTarget(const proto::SpellEntry& spell);

	protected:

		// =====================================================================
		// Actions - Spells
		// =====================================================================

		/**
		 * @brief Casts a spell on a specific target by spell ID.
		 * @param spellId The spell entry ID to cast.
		 * @param target The target unit.
		 * @return True if the cast was initiated.
		 */
		bool CastSpellOn(uint32 spellId, GameUnitS& target);

		/**
		 * @brief Casts a spell on the creature itself.
		 * @param spellId The spell entry ID to cast.
		 * @return True if the cast was initiated.
		 */
		bool CastSpellOnSelf(uint32 spellId);

		/**
		 * @brief Casts a spell on the current victim.
		 * @param spellId The spell entry ID to cast.
		 * @return True if the cast was initiated.
		 */
		bool CastSpellOnVictim(uint32 spellId);

		// =====================================================================
		// Actions - Movement
		// =====================================================================

		/**
		 * @brief Moves the creature to a world position.
		 * @param position The target position.
		 * @param range Acceptable distance from the position (default: 0.5).
		 */
		void MoveTo(const Vector3& position, float range = 0.5f);

		/**
		 * @brief Stops all movement immediately.
		 */
		void StopMovement();

		// =====================================================================
		// Actions - Chat
		// =====================================================================

		/**
		 * @brief Makes the creature say a message (local chat).
		 * @param text The message text.
		 */
		void Say(const String& text);

		/**
		 * @brief Makes the creature yell a message (wide-range chat).
		 * @param text The message text.
		 */
		void Yell(const String& text);

		// =====================================================================
		// Actions - State Modification
		// =====================================================================

		/**
		 * @brief Sets the creature's immune state.
		 * @param immune True to make the creature immune to damage.
		 *
		 * When immune, all incoming damage is ignored.
		 */
		void SetImmune(bool immune);

		/**
		 * @brief Sets the creature's not-attackable state.
		 * @param notAttackable True to make the creature unable to be targeted.
		 *
		 * Makes the creature un-targetable by hostile actions.
		 */
		void SetNotAttackable(bool notAttackable);

		/**
		 * @brief Stops the creature's auto-attack.
		 */
		void StopAutoAttack();

		/**
		 * @brief Starts auto-attacking a target.
		 * @param target The unit to auto-attack.
		 */
		void StartAutoAttack(GameUnitS& target);

		/**
		 * @brief Sets the virtual equipment display for a slot.
		 * @param slot The equipment slot (0=mainhand, 1=offhand, 2=ranged).
		 * @param displayId The item display ID to show.
		 */
		void SetVirtualEquipment(uint8 slot, uint32 displayId);

		/**
		 * @brief Sets the creature's health to a specific percentage.
		 * @param percent The health percentage (0-100).
		 */
		void SetHealthPercent(uint8 percent);

		// =====================================================================
		// Phase Management
		// =====================================================================

		/**
		 * @brief Gets the current combat phase.
		 * @return The current phase number (starts at 0).
		 */
		uint32 GetPhase() const;

		/**
		 * @brief Sets the current combat phase.
		 * @param phase The new phase number.
		 *
		 * Fires OnPhaseChanged() if the phase actually changes.
		 */
		void SetPhase(uint32 phase);

		// =====================================================================
		// Timer Management
		// =====================================================================

		/**
		 * @brief Schedules a timer that fires OnTimerExpired().
		 * @param timerId A unique ID for this timer.
		 * @param delayMs Delay in milliseconds.
		 *
		 * If a timer with the same ID already exists, it is replaced.
		 */
		void ScheduleTimer(uint32 timerId, uint32 delayMs);

		/**
		 * @brief Cancels a previously scheduled timer.
		 * @param timerId The timer ID to cancel.
		 */
		void CancelTimer(uint32 timerId);

		/**
		 * @brief Cancels all active timers.
		 */
		void CancelAllTimers();

		// =====================================================================
		// Health Threshold Management
		// =====================================================================

		/**
		 * @brief Registers a health percentage threshold to watch.
		 * @param percentage The health percentage (1-99).
		 *
		 * When the creature's health drops to or below this percentage,
		 * OnHealthThresholdReached() is called exactly once per combat.
		 */
		void RegisterHealthThreshold(uint8 percentage);

		/**
		 * @brief Checks and fires any pending health thresholds.
		 *
		 * Called automatically by the combat state after damage is taken.
		 */
		void CheckHealthThresholds();

		// =====================================================================
		// Target Selection Helpers
		// =====================================================================

		/**
		 * @brief Gets the current main target (highest threat).
		 * @return Pointer to the current victim, or nullptr.
		 */
		GameUnitS* GetCurrentVictim() const;

		/**
		 * @brief Finds the player furthest from the creature on the threat list.
		 * @return Pointer to the furthest player, or nullptr.
		 */
		GameUnitS* GetFurthestPlayer() const;

		/**
		 * @brief Finds the player closest to the creature on the threat list.
		 * @return Pointer to the closest player, or nullptr.
		 */
		GameUnitS* GetClosestPlayer() const;

		/**
		 * @brief Picks a random player from the threat list.
		 * @return Pointer to a random player, or nullptr.
		 */
		GameUnitS* GetRandomPlayer() const;

		/**
		 * @brief Finds a player positioned behind the creature.
		 * @return Pointer to a player behind the creature, or nullptr.
		 */
		GameUnitS* GetPlayerBehind() const;

		/**
		 * @brief Gets all players within a given range.
		 * @param range The search range in world units.
		 * @return Vector of players within range.
		 */
		std::vector<GameUnitS*> GetPlayersInRange(float range) const;

		/**
		 * @brief Gets all living units on the threat list.
		 * @return Vector of all threat targets.
		 */
		std::vector<GameUnitS*> GetAllThreatTargets() const;

		// =====================================================================
		// State Queries
		// =====================================================================

		/**
		 * @brief Gets a reference to the controlled creature.
		 * @return Reference to the GameCreatureS.
		 */
		GameCreatureS& GetCreature() const;

		/**
		 * @brief Gets the creature's current health percentage.
		 * @return Health percentage (0.0 - 100.0).
		 */
		float GetHealthPercent() const;

		/**
		 * @brief Whether the creature is currently moving.
		 * @return True if moving.
		 */
		bool IsMoving() const;

		/**
		 * @brief Whether the creature is currently casting a spell.
		 * @return True if casting.
		 */
		bool IsCasting() const;

		/**
		 * @brief Gets the number of units on the threat list.
		 * @return Threat list size.
		 */
		uint32 GetThreatCount() const;

		/**
		 * @brief Gets the distance from the creature to its home position.
		 * @return Distance in world units.
		 */
		float GetDistanceToHome() const;

		/**
		 * @brief Gets the distance from the creature to a target.
		 * @param target The target unit.
		 * @return Distance in world units.
		 */
		float GetDistanceToTarget(const GameUnitS& target) const;

		// =====================================================================
		// Threat Management
		// =====================================================================

		/**
		 * @brief Adds or removes threat on a target.
		 * @param target The target unit.
		 * @param amount Amount of threat to add (can be negative).
		 */
		void ModifyThreat(GameUnitS& target, float amount);

		/**
		 * @brief Resets all threat to zero on all targets.
		 */
		void ResetAllThreat();

		/**
		 * @brief Gets a reference to the owning combat state.
		 * @return Reference to the CreatureAICombatState.
		 */
		CreatureAICombatState& GetCombatState() const;

	private:

		/// The combat state that owns this script.
		CreatureAICombatState& m_combatState;

		/// Current combat phase (0 = default / phase 1).
		uint32 m_currentPhase;

		/// Health percentage thresholds to watch for.
		std::set<uint8> m_healthThresholds;

		/// Health thresholds that have already fired this combat.
		std::set<uint8> m_firedThresholds;

		/// Active timers mapped by timer ID.
		std::map<uint32, std::unique_ptr<Countdown>> m_timers;
	};

	/// Factory function type for creating combat scripts.
	using CombatScriptFactory = std::function<std::unique_ptr<CreatureCombatScript>(CreatureAICombatState&)>;
}
