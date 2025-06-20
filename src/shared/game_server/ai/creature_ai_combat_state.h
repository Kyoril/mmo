// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "creature_ai_state.h"
#include "base/countdown.h"
#include "objects/game_unit_s.h"
#include "math/vector3.h"

namespace mmo
{	/// Handle the combat state of a creature AI. In this state, the unit manages
	/// threat, victim selection, and combat movement against hostile units.
	class CreatureAICombatState : public CreatureAIState
	{
	private:
		/// Represents an entry in the threat list of this unit.
		struct ThreatEntry
		{
			/// Threatening unit.
			std::weak_ptr<GameUnitS> threatener;
			/// Threat amount for this unit.
			float amount;

			/**
			 * @brief Constructs a new threat entry.
			 * @param threatener The threatening unit.
			 * @param amount Initial threat amount.
			 */
			explicit ThreatEntry(GameUnitS& threatener, float amount = 0.0f)
				: threatener(std::static_pointer_cast<GameUnitS>(threatener.shared_from_this()))
				, amount(amount)
			{
			}
		};
		
		/// Manages movement state to prevent unnecessary path recalculation.
		struct MovementState
		{
			/// Current target position being moved to.
			Vector3 targetPosition;
			/// Combat range used for the current movement.
			float combatRange;
			/// Whether we are currently moving to a valid combat position.
			bool isMovingToCombat;

			/**
			 * @brief Constructs a new movement state.
			 */
			MovementState()
				: targetPosition(Vector3::Zero)
				, combatRange(0.0f)
				, isMovingToCombat(false)
			{
			}

			/**
			 * @brief Checks if current movement is still valid for the given target.
			 * @param target The target unit to check against.
			 * @param currentCombatRange The current combat range.
			 * @return True if movement is still valid.
			 */
			bool IsValidFor(const GameUnitS& target, float currentCombatRange) const;

			/**
			 * @brief Updates the movement state for a new target.
			 * @param target The new target position.
			 * @param range The combat range to use.
			 */
			void UpdateTarget(const Vector3& target, float range);

			/**
			 * @brief Resets the movement state.
			 */
			void Reset();
		};

		typedef std::map<uint64, ThreatEntry> ThreatList;
		typedef std::map<uint64, scoped_connection> UnitSignals;
		typedef std::map<uint64, scoped_connection_container> UnitSignals2;
	public:
		/**
		 * @brief Initializes a new instance of the CreatureAICombatState class.
		 * @param ai The ai class instance this state belongs to.
		 * @param victim The initial victim that triggered combat.
		 */
		explicit CreatureAICombatState(CreatureAI& ai, GameUnitS& victim);

		/**
		 * @brief Default destructor.
		 */
		~CreatureAICombatState() override;

	public:
		/**
		 * @brief Executed when the AI state is activated.
		 */
		void OnEnter() override;

		/**
		 * @brief Executed when the AI state becomes inactive.
		 */
		void OnLeave() override;

		/**
		 * @brief Executed when the controlled unit was damaged by a known attacker.
		 * @param attacker The unit which damaged the controlled unit.
		 */
		void OnDamage(GameUnitS& attacker) override;

		/**
		 * @brief Executed when combat movement for the controlled unit is enabled or disabled.
		 */
		void OnCombatMovementChanged() override;

		/**
		 * @brief Executed when the controlled unit moved.
		 */
		void OnControlledMoved() override;

	private:
		// === Threat Management ===

		/**
		 * @brief Adds threat of an attacker to the threat list.
		 * @param threatener The unit which generated threat.
		 * @param amount Amount of threat which will be added. May be 0.0 to simply add the unit to the threat list.
		 */
		void AddThreat(GameUnitS& threatener, float amount);

		/**
		 * @brief Removes a unit from the threat list. This may change the AI state.
		 * @param threatener The unit that will be removed from the threat list.
		 */
		void RemoveThreat(GameUnitS& threatener);

		/**
		 * @brief Gets the amount of threat of an attacking unit.
		 * @param threatener The unit whose threat-value is requested.
		 * @return Threat value of the attacking unit, which may be 0.
		 */
		float GetThreat(const GameUnitS& threatener) const;

		/**
		 * @brief Sets the amount of threat of an attacking unit.
		 * @param threatener The attacking unit whose threat value should be set.
		 * @param amount The new total threat amount, which may be 0.
		 */
		void SetThreat(const GameUnitS& threatener, float amount);

		/**
		 * @brief Determines the unit with the most amount of threat.
		 * @return Pointer of the unit with the highest threat-value or nullptr.
		 */
		GameUnitS* GetTopThreatener() const;

		/**
		 * @brief Cleans up expired threat entries and their associated signals.
		 */
		void CleanupExpiredThreats();

		// === Victim Management ===

		/**
		 * @brief Updates the current victim of the controlled unit based on the current threat table.
		 */
		void UpdateVictim();

		// === Movement Management ===

		/**
		 * @brief Checks if movement to the target is necessary and efficient.
		 * @param target The unit to check movement for.
		 * @return True if movement is needed, false otherwise.
		 */
		bool ShouldMoveToTarget(const GameUnitS& target) const;

		/**
		 * @brief Starts chasing a unit so that the controlled unit is in melee hit range.
		 * @param target The unit to chase.
		 * @return True if movement was initiated successfully, false otherwise.
		 */
		bool ChaseTarget(GameUnitS& target);

		/**
		 * @brief Handles movement failure and stuck detection.
		 * @return True if the unit is considered stuck and AI should reset.
		 */
		bool HandleMovementFailure();

		// === Combat Logic ===

		/**
		 * @brief Determines the next action to execute.
		 */
		void ChooseNextAction();

		/**
		 * @brief Checks if the unit should reset due to distance or timeout constraints.
		 * @param victim Current victim, if any.
		 * @return True if AI should reset, false otherwise.
		 */
		bool ShouldResetAI(const GameUnitS* victim) const;

		// === Initialization Helpers ===

		/**
		 * @brief Sets up event connections for the combat state.
		 */
		void SetupEventConnections();

		/**
		 * @brief Sets up reset conditions based on world instance type.
		 */
		void SetupResetConditions();

		/**
		 * @brief Sets up signals for a specific threatener.
		 * @param threatener The threatening unit.
		 * @param guid The GUID of the threatening unit.
		 */
		void SetupThreatenerSignals(GameUnitS& threatener, uint64 guid);	private:
		// === Core State ===
		std::weak_ptr<GameUnitS> m_combatInitiator;
		ThreatList m_threat;
		MovementState m_movementState;
		
		// === Timing and Counters ===
		GameTime m_lastThreatTime;
		Countdown m_nextActionCountdown;
		uint32 m_stuckCounter;
		
		// === Flags ===
		bool m_isCasting;
		bool m_entered;
		bool m_isRanged;
		bool m_canReset;
		
		// === Event Connections ===
		UnitSignals m_killedSignals;
		UnitSignals2 m_miscSignals;
		scoped_connection m_onThreatened;
		scoped_connection m_onMoveTargetChanged;
		scoped_connection m_getThreat;
		scoped_connection m_setThreat;
		scoped_connection m_getTopThreatener;
		scoped_connection m_onUnitStateChanged;
		scoped_connection m_onAutoAttackDone;
				// === Constants ===
		static constexpr float RESET_DISTANCE_SQ = 60.0f * 60.0f;
		static constexpr uint32 RESET_TIMEOUT_MS = 10000;  // 10 seconds
		static constexpr uint32 MAX_STUCK_COUNT = 20;
		static constexpr uint32 ACTION_INTERVAL_MS = 500;
		/// Movement target range factor (0.75 = move to 75% of attack range to ensure we're close enough)
		static constexpr float COMBAT_RANGE_FACTOR = 0.75f;
	};
}
