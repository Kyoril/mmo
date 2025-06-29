// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "creature_ai_state.h"
#include "base/countdown.h"
#include "objects/game_unit_s.h"
#include "math/vector3.h"
#include "game/spell_target_map.h"

namespace mmo
{
	/**
	 * @brief Handles the combat state of a creature AI with intelligent behavior types.
	 * 
	 * This class implements a sophisticated combat AI system that supports three distinct
	 * behavior types based on the creature's spell configuration:
	 * 
	 * **Combat Behavior Types:**
	 * - **Melee**: Traditional close-combat behavior with auto-attacks and melee spells
	 * - **Caster**: Ranged spellcaster that maintains distance and prioritizes spell casting
	 * - **Ranged**: Similar to caster but focuses on ranged attacks and abilities
	 * 
	 * **Key Features:**
	 * - **Automatic Behavior Detection**: Analyzes creature spells to determine optimal behavior
	 * - **Intelligent Range Management**: Maintains appropriate distance based on behavior type
	 * - **Spell Priority System**: Prioritizes spells by configured priority values
	 * - **Power Management**: Tracks mana/energy and falls back to melee when depleted
	 * - **Cooldown Management**: Tracks and respects spell cooldowns
	 * - **Dynamic Positioning**: Moves to optimal range for spell casting or melee combat
	 * 
	 * **Behavior Logic:**
	 * - Casters and ranged units maintain distance and cast spells as primary actions
	 * - Melee units chase targets and use spells as supplementary abilities
	 * - All units fall back to melee combat when out of power or suitable spells
	 * - Movement is optimized to reduce unnecessary path recalculation
	 * 
	 * The system integrates seamlessly with existing threat management, victim selection,
	 * and combat movement systems while adding intelligent spell-based combat behaviors.
	 */
	class CreatureAICombatState : public CreatureAIState
	{
	public:
		/// Defines the combat behavior type of a creature.
		enum class CombatBehavior
		{
			/// Default melee combat behavior - moves to melee range and auto-attacks
			Melee,
			/// Caster behavior - maintains range and prioritizes spell casting
			Caster,
			/// Ranged behavior - similar to caster but focuses on ranged attacks
			Ranged
		};

		/// Represents a spell that a creature can cast in combat.
		struct CreatureSpell
		{
			const proto::SpellEntry* spell;
			GameTime lastCastTime;
			GameTime cooldownEnd;
			float minRange;
			float maxRange;
			uint32 priority;
			bool canCast;

			/**
			 * @brief Constructs a new creature spell entry.
			 * @param spellEntry The spell entry.
			 * @param minRange Minimum casting range.
			 * @param maxRange Maximum casting range.
			 * @param priority Spell priority (higher = more important).
			 */
			explicit CreatureSpell(const proto::SpellEntry* spellEntry, float minRange = 0.0f, float maxRange = 30.0f, uint32 priority = 100)
				: spell(spellEntry)
				, lastCastTime(0)
				, cooldownEnd(0)
				, minRange(minRange)
				, maxRange(maxRange)
				, priority(priority)
				, canCast(true)
			{
			}
		};

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

		/**
		 * @brief Executed when a spell cast ends for the controlled unit.
		 * @param succeeded Whether the spell cast succeeded.
		 */
		void OnSpellCastEnded(bool succeeded);

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
		 * @brief Predicts where the target will be based on their current movement.
		 * @param target The unit to predict position for.
		 * @return Predicted position of the target.
		 */
		Vector3 PredictTargetPosition(const GameUnitS& target) const;

		/**
		 * @brief Handles movement failure and stuck detection.
		 * @return True if the unit is considered stuck and AI should reset.
		 */
		bool HandleMovementFailure();

		// === Combat Logic ===

		/**
		 * @brief Determines the combat behavior type based on available spells and creature configuration.
		 * @return The combat behavior type for this creature.
		 */
		CombatBehavior DetermineCombatBehavior() const;

		/**
		 * @brief Checks if the creature can cast spells (has mana/power and available spells).
		 * @return True if the creature can cast spells.
		 */
		bool CanCastSpells() const;

		/**
		 * @brief Selects the best spell to cast based on current situation.
		 * @param target The current target.
		 * @return Pointer to the best spell, or nullptr if no suitable spell.
		 */
		const CreatureSpell* SelectBestSpell(const GameUnitS& target) const;

		/**
		 * @brief Attempts to cast a spell at the target.
		 * @param spell The spell to cast.
		 * @param target The target to cast at.
		 * @return True if spell casting was initiated successfully.
		 */
		bool CastSpell(const CreatureSpell& spell, GameUnitS& target);

		/**
		 * @brief Updates spell cooldowns and availability.
		 */
		void UpdateSpellCooldowns();

		/**
		 * @brief Checks if the creature is in optimal range for its behavior type.
		 * @param target The target to check range for.
		 * @return True if in optimal range.
		 */
		bool IsInOptimalRange(const GameUnitS& target) const;

		/**
		 * @brief Moves to optimal range based on combat behavior.
		 * @param target The target to position for.
		 * @return True if movement was initiated successfully.
		 */
		bool MoveToOptimalRange(GameUnitS& target);

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
		 * @brief Initializes the available spells from creature entry.
		 */
		void InitializeSpells();

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
		
		// === Spell Management ===
		std::vector<CreatureSpell> m_availableSpells;
		CombatBehavior m_combatBehavior;
		GameTime m_lastSpellCastTime;
		
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
		scoped_connection m_onSpellCastStarted;
		
		// === Casting Timeout ===
		GameTime m_castingTimeoutEnd;
				// === Constants ===
		static constexpr float RESET_DISTANCE_SQ = 60.0f * 60.0f;
		static constexpr uint32 RESET_TIMEOUT_MS = 10000;  // 10 seconds
		static constexpr uint32 MAX_STUCK_COUNT = 20;
		static constexpr uint32 ACTION_INTERVAL_MS = 500;
		/// Movement target range factor (0.75 = move to 75% of attack range to ensure we're close enough)
		static constexpr float COMBAT_RANGE_FACTOR = 0.75f;
		/// Optimal caster range (distance to maintain from target)
		static constexpr float CASTER_OPTIMAL_RANGE = 20.0f;
		/// Minimum distance to maintain from target for casters
		static constexpr float CASTER_MIN_RANGE = 8.0f;
		/// Maximum spell casting range
		static constexpr float MAX_SPELL_RANGE = 30.0f;
	};
}
