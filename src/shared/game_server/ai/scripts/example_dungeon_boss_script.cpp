// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "example_dungeon_boss_script.h"
#include "game_server/ai/creature_ai_combat_state.h"
#include "objects/game_creature_s.h"
// Also need the registry header for the macro
#include "game_server/ai/creature_combat_script_registry.h"

namespace mmo
{
	ExampleDungeonBossScript::ExampleDungeonBossScript(CreatureAICombatState& combatState)
		: CreatureCombatScript(combatState)
		, m_isTransitioning(false)
		, m_hasEnraged(false)
	{
	}

	ExampleDungeonBossScript::~ExampleDungeonBossScript() = default;

	void ExampleDungeonBossScript::OnCombatStart()
	{
		m_isTransitioning = false;
		m_hasEnraged = false;

		// Start in Phase 1
		SetPhase(PHASE_WARLORD);

		// Register the health threshold for the phase transition
		RegisterHealthThreshold(50);

		// Register the enrage threshold
		RegisterHealthThreshold(10);

		// Boss yells on aggro
		Yell("You dare challenge me? You will fall!");

		// Schedule Phase 1 abilities
		ScheduleTimer(TIMER_CLEAVE, CLEAVE_INTERVAL_MS);
		ScheduleTimer(TIMER_CHARGE, CHARGE_INTERVAL_PHASE1_MS);
		ScheduleTimer(TIMER_TAUNT, TAUNT_INTERVAL_MS);
	}

	void ExampleDungeonBossScript::OnCombatEnd()
	{
		// Clean up: remove immunity and other states
		SetImmune(false);
		SetNotAttackable(false);
		m_isTransitioning = false;

		CancelAllTimers();
	}

	bool ExampleDungeonBossScript::OnChooseAction()
	{
		// During transition, the boss does nothing
		if (m_isTransitioning)
		{
			return true; // Script handles it (by doing nothing)
		}

		// Delegate to phase-specific logic
		switch (GetPhase())
		{
		case PHASE_WARLORD:
			DoPhase1Action();
			break;

		case PHASE_BERSERKER:
			DoPhase2Action();
			break;

		default:
			break;
		}

		// Return false to let default AI handle basic melee/movement
		// Our phase-specific logic only handles special abilities via timers
		return false;
	}

	void ExampleDungeonBossScript::OnHealthThresholdReached(uint8 percentage)
	{
		if (percentage == 50 && GetPhase() == PHASE_WARLORD)
		{
			// === Begin Phase Transition ===
			Yell("ENOUGH! You will witness my true power!");

			// Enter transition phase
			SetPhase(PHASE_TRANSITION);

			// Cancel Phase 1 timers
			CancelTimer(TIMER_CLEAVE);
			CancelTimer(TIMER_CHARGE);

			// Make the boss immune and un-attackable during transformation
			m_isTransitioning = true;
			SetImmune(true);
			SetNotAttackable(true);

			// Stop attacking and moving
			StopAutoAttack();
			StopMovement();

			// Set health to exactly 50% in case damage overkilled past the threshold
			SetHealthPercent(50);

			// Schedule the end of the transformation
			ScheduleTimer(TIMER_TRANSFORM_COMPLETE, TRANSFORM_DURATION_MS);
		}
		else if (percentage == 10 && GetPhase() == PHASE_BERSERKER && !m_hasEnraged)
		{
			// === Enrage at 10% HP ===
			m_hasEnraged = true;
			Yell("I WILL DESTROY YOU ALL!");

			// Cast enrage buff on self (if spell ID is configured)
			if (SPELL_ENRAGE != 0)
			{
				CastSpellOnSelf(SPELL_ENRAGE);
			}
		}
	}

	void ExampleDungeonBossScript::OnTimerExpired(uint32 timerId)
	{
		switch (timerId)
		{
		case TIMER_CLEAVE:
			{
				// Cast cleave on current target
				if (SPELL_CLEAVE != 0 && !IsCasting())
				{
					CastSpellOnVictim(SPELL_CLEAVE);
				}

				// Reschedule based on current phase
				if (GetPhase() == PHASE_WARLORD)
				{
					ScheduleTimer(TIMER_CLEAVE, CLEAVE_INTERVAL_MS);
				}
			}
			break;

		case TIMER_CHARGE:
			{
				if (!IsCasting() && !m_isTransitioning)
				{
					DoChargeAttack();
				}

				// Reschedule with phase-appropriate interval
				const uint32 interval = (GetPhase() == PHASE_BERSERKER)
					? CHARGE_INTERVAL_PHASE2_MS
					: CHARGE_INTERVAL_PHASE1_MS;
				ScheduleTimer(TIMER_CHARGE, interval);
			}
			break;

		case TIMER_TRANSFORM_COMPLETE:
			{
				// === Transformation Complete ===
				Yell("Behold my true form! Now you die!");

				// Change equipment to represent the transformation
				// Slot 0 = main hand. Use your actual item display IDs.
				SetVirtualEquipment(0, 0); // Replace 0 with actual display ID

				// Remove immunity
				m_isTransitioning = false;
				SetImmune(false);
				SetNotAttackable(false);

				// Enter Phase 2
				SetPhase(PHASE_BERSERKER);
			}
			break;

		case TIMER_GROUND_SLAM:
			{
				// AoE ground slam
				if (SPELL_GROUND_SLAM != 0 && !IsCasting())
				{
					Say("The ground trembles...");
					CastSpellOnSelf(SPELL_GROUND_SLAM);
				}

				// Reschedule
				if (GetPhase() == PHASE_BERSERKER)
				{
					ScheduleTimer(TIMER_GROUND_SLAM, GROUND_SLAM_INTERVAL_MS);
				}
			}
			break;

		case TIMER_TAUNT:
			{
				// Random combat taunts
				static const char* phase1Taunts[] =
				{
					"Is that all you've got?",
					"You fight like children!",
					"My blade will taste your blood!"
				};

				static const char* phase2Taunts[] =
				{
					"There is no escape!",
					"Your bones will decorate my throne!",
					"FEEL MY WRATH!"
				};

				if (GetPhase() == PHASE_BERSERKER)
				{
					Yell(phase2Taunts[GetAsyncTimeMs() % 3]);
				}
				else if (GetPhase() == PHASE_WARLORD)
				{
					Say(phase1Taunts[GetAsyncTimeMs() % 3]);
				}

				ScheduleTimer(TIMER_TAUNT, TAUNT_INTERVAL_MS);
			}
			break;

		case TIMER_ENRAGE:
			break;

		default:
			break;
		}
	}

	void ExampleDungeonBossScript::OnPhaseChanged(uint32 oldPhase, uint32 newPhase)
	{
		DLOG("DungeonBoss: Phase changed from " << oldPhase << " to " << newPhase);

		if (newPhase == PHASE_BERSERKER)
		{
			// Schedule Phase 2 abilities
			ScheduleTimer(TIMER_GROUND_SLAM, GROUND_SLAM_INTERVAL_MS);
			ScheduleTimer(TIMER_CHARGE, CHARGE_INTERVAL_PHASE2_MS);
			ScheduleTimer(TIMER_TAUNT, TAUNT_INTERVAL_MS);

			// Reset threat so the boss re-evaluates targets
			ResetAllThreat();

			// Resume attacking the closest player
			if (auto* closest = GetClosestPlayer())
			{
				StartAutoAttack(*closest);
			}
		}
	}

	bool ExampleDungeonBossScript::CanDie() const
	{
		// Boss can die normally (unlike training dummies)
		return true;
	}

	bool ExampleDungeonBossScript::CanMove() const
	{
		// Can't move during transformation
		return !m_isTransitioning;
	}

	void ExampleDungeonBossScript::DoPhase1Action()
	{
		// Phase 1 special abilities are handled by timers.
		// The default AI handles basic melee combat and movement.
		// Nothing extra to do here on each tick.
	}

	void ExampleDungeonBossScript::DoPhase2Action()
	{
		// Phase 2 special abilities are handled by timers.
		// The default AI handles melee combat.
		// Nothing extra to do here on each tick.
	}

	void ExampleDungeonBossScript::DoChargeAttack()
	{
		// Pick the furthest player for charge target
		GameUnitS* target = GetFurthestPlayer();
		if (!target)
		{
			// Fallback to random player
			target = GetRandomPlayer();
		}

		if (!target)
		{
			return;
		}

		// Yell at the charge target
		Yell("You cannot escape me!");

		// If we have a charge spell, cast it on the target
		if (SPELL_CHARGE != 0)
		{
			CastSpellOn(SPELL_CHARGE, *target);
		}
		else
		{
			// Fallback: manually move to the target's position quickly
			MoveTo(target->GetPosition(), 1.0f);
		}
	}
}
