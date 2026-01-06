// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_context.h"
#include "bot_unit.h"
#include "bot_actions/wait_action.h"
#include "bot_actions/start_attack_action.h"
#include "bot_actions/stop_attack_action.h"
#include "bot_actions/move_to_unit_action.h"
#include "bot_actions/face_unit_action.h"

#include "log/default_log_levels.h"

#include <chrono>

using namespace std::chrono_literals;

namespace mmo
{
	/// @brief A demonstration profile that engages in melee combat.
	/// 
	/// This profile demonstrates the combat system:
	/// - Finds and attacks nearest attackable creature
	/// - Moves to targets that are out of melee range
	/// - Faces targets when needed
	/// - Logs combat events (swings, errors, damage dealt/received)
	/// - Automatically re-targets when current target dies
	class CombatProfile final : public BotProfile
	{
	public:
		/// @brief Constructs the combat profile.
		/// @param searchRadius The radius to search for attackable creatures (default 100 yards).
		/// @param combatCheckInterval How often to check for new targets.
		explicit CombatProfile(
			float searchRadius = 100.0f,
			std::chrono::milliseconds combatCheckInterval = 2000ms)
			: m_searchRadius(searchRadius)
			, m_combatCheckInterval(combatCheckInterval)
		{
		}

		std::string GetName() const override
		{
			return "Combat";
		}

		// ============================================================
		// Combat Event Overrides
		// ============================================================

		void OnAttackSwing(BotContext& context, uint64 targetGuid, uint32 damage, uint32 hitInfo, uint32 victimState) override
		{
			std::string hitType = "hit";

			// Check hit info flags
			if (hitInfo & 0x00000002) // hit_info::Miss
			{
				hitType = "MISSED";
			}
			else if (hitInfo & 0x00000010) // hit_info::CriticalHit
			{
				hitType = "CRIT";
			}
			else if (hitInfo & 0x00000020) // hit_info::Glancing
			{
				hitType = "glancing";
			}
			else if (hitInfo & 0x00000040) // hit_info::Crushing
			{
				hitType = "CRUSHING";
			}

			// Check victim state
			std::string victimReaction;
			switch (victimState)
			{
			case 1: // Dodge
				victimReaction = " (DODGED)";
				break;
			case 2: // Parry
				victimReaction = " (PARRIED)";
				break;
			case 3: // Block
				victimReaction = " (BLOCKED)";
				break;
			default:
				break;
			}

			ILOG("[COMBAT] Attack " << hitType << " for " << damage << " damage" << victimReaction);
		}

		void OnAttackSwingError(BotContext& context, AttackSwingEvent error) override
		{
			std::string errorName;
			switch (error)
			{
			case attack_swing_event::NotStanding:
				errorName = "Not Standing";
				break;
			case attack_swing_event::OutOfRange:
				errorName = "Out of Range";
				// Move closer to target
				if (m_currentTargetGuid != 0)
				{
					ILOG("[COMBAT] Target out of range - moving closer");
					QueueAction(std::make_shared<MoveToUnitAction>(m_currentTargetGuid, m_meleeRange - 1.0f));
				}
				break;
			case attack_swing_event::CantAttack:
				errorName = "Can't Attack";
				break;
			case attack_swing_event::WrongFacing:
				errorName = "Wrong Facing";
				// Face the target
				if (m_currentTargetGuid != 0)
				{
					ILOG("[COMBAT] Facing wrong direction - turning to target");
					context.FaceUnit(m_currentTargetGuid);
				}
				break;
			case attack_swing_event::TargetDead:
				errorName = "Target Dead";
				// Clear target and find new one
				m_currentTargetGuid = 0;
				break;
			default:
				errorName = "Unknown Error";
				break;
			}

			WLOG("[COMBAT] Attack error: " << errorName);
		}

		void OnDamagedUnit(BotContext& context, uint64 targetGuid, uint32 damage, bool isCrit) override
		{
			ILOG("[COMBAT] Dealt " << damage << " damage" << (isCrit ? " (CRIT)" : ""));
		}

		void OnDamaged(BotContext& context, uint32 damage, uint8 flags) override
		{
			bool isCrit = (flags & 1) != 0;
			WLOG("[COMBAT] Took " << damage << " damage" << (isCrit ? " (CRIT!)" : ""));

			// Check our health
			const BotUnit* self = context.GetSelf();
			if (self)
			{
				float healthPct = self->GetHealthPercent() * 100.0f;
				if (healthPct < 30.0f)
				{
					WLOG("[COMBAT] LOW HEALTH WARNING: " << static_cast<int>(healthPct) << "% HP remaining!");
				}
			}
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Combat profile activated - searching for hostiles within " << m_searchRadius << " yards");

			// Start with a short delay
			QueueAction(std::make_shared<WaitAction>(1000ms));
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// Check combat state
			UpdateCombat(context);

			// Queue next check
			QueueAction(std::make_shared<WaitAction>(m_combatCheckInterval));
			return true;
		}

	private:
		void UpdateCombat(BotContext& context)
		{
			const BotUnit* self = context.GetSelf();
			if (!self)
			{
				WLOG("[COMBAT] No self unit!");
				return;
			}

			// Check if we're dead
			if (self->IsDead())
			{
				if (context.IsAutoAttacking())
				{
					ILOG("[COMBAT] We died - stopping attack");
					context.StopAutoAttack();
				}
				return;
			}

			// Check current target
			if (m_currentTargetGuid != 0)
			{
				const BotUnit* target = context.GetUnit(m_currentTargetGuid);

				// Target still valid?
				if (target && !target->IsDead())
				{
					float distance = target->GetDistanceTo(self->GetPosition());

					// If we're out of melee range, move closer
					if (distance > m_meleeRange)
					{
						DLOG("[COMBAT] Target at " << distance << " yards - moving closer");
						QueueAction(std::make_shared<MoveToUnitAction>(m_currentTargetGuid, m_meleeRange - 1.0f));
						return;
					}

					// We're in range - make sure we're attacking
					if (!context.IsAutoAttacking())
					{
						ILOG("[COMBAT] Re-engaging target at " << distance << " yards");
						context.StartAutoAttack(m_currentTargetGuid);
					}
					return;
				}

				// Target died or despawned
				ILOG("[COMBAT] Target lost - searching for new target");
				m_currentTargetGuid = 0;
			}

			// Find new target within our search radius
			const BotUnit* newTarget = context.GetNearestAttackable(m_searchRadius);
			if (newTarget && !newTarget->IsDead())
			{
				float distance = newTarget->GetDistanceTo(self->GetPosition());
				ILOG("[COMBAT] Found target: Entry " << newTarget->GetEntry()
					<< " Level " << newTarget->GetLevel()
					<< " at " << distance << " yards"
					<< " (" << static_cast<int>(newTarget->GetHealthPercent() * 100) << "% HP)");

				m_currentTargetGuid = newTarget->GetGuid();

				// If in range, attack immediately; otherwise queue movement
				if (distance <= m_meleeRange)
				{
					context.StartAutoAttack(m_currentTargetGuid);
				}
				else
				{
					ILOG("[COMBAT] Moving to engage target");
					QueueAction(std::make_shared<MoveToUnitAction>(m_currentTargetGuid, m_meleeRange - 1.0f));
				}
			}
			else if (context.IsAutoAttacking())
			{
				ILOG("[COMBAT] No targets in range - stopping attack");
				context.StopAutoAttack();
			}
		}

	private:
		float m_searchRadius;
		float m_meleeRange { 5.0f }; // Melee attack range in yards
		std::chrono::milliseconds m_combatCheckInterval;
		uint64 m_currentTargetGuid { 0 };
	};

}
