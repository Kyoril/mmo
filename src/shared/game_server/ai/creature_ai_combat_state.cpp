// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <algorithm>

#include "game_server/ai/creature_ai_combat_state.h"
#include "game_server/ai/creature_ai.h"
#include "objects/game_creature_s.h"
#include "game_server/world/universe.h"
#include "log/default_log_levels.h"

namespace mmo
{
	CreatureAICombatState::CreatureAICombatState(CreatureAI& ai, GameUnitS& victim)
		: CreatureAIState(ai)
		, m_combatInitiator(std::static_pointer_cast<GameUnitS>(victim.shared_from_this()))
		, m_lastThreatTime(0)
		, m_stuckCounter(0)
		, m_nextActionCountdown(ai.GetControlled().GetTimers())
		, m_isCasting(false)
		, m_entered(false)
		, m_isRanged(false)
		, m_canReset(false)
	{
	}

	CreatureAICombatState::~CreatureAICombatState()
	{
	}

	void CreatureAICombatState::OnEnter()
	{
		CreatureAIState::OnEnter();
		
		m_stuckCounter = 0;

		auto& controlled = GetControlled();
		controlled.RemoveAllCombatParticipants();

		const std::shared_ptr<GameUnitS> initiator = m_combatInitiator.lock();
		AddThreat(*initiator, 0.0f);
		m_combatInitiator.reset();

		m_nextActionCountdown.ended.connect(this, &CreatureAICombatState::ChooseNextAction);

		controlled.SetInCombat(true, false);

		// Watch for threat events
		m_onThreatened = controlled.threatened.connect([this](GameUnitS& threatener, float amount)
		{
			AddThreat(threatener, amount);
		});

		ASSERT(controlled.GetWorldInstance());

		// Reset AI if target is out of range, but only in non-instanced-pve-areas
		if (!controlled.GetWorldInstance()->IsInstancedPvE())
		{
			m_canReset = true;

			m_onMoveTargetChanged = GetControlled().GetMover().targetChanged.connect([this]()
			{
				auto& homePos = GetAI().GetHome().position;
				const auto& controlled = GetControlled();

				if (auto* victim = controlled.GetVictim())
				{
					// Target flying / swimming?
					if (victim->GetMovementInfo().movementFlags & (movement_flags::Flying | movement_flags::Swimming))
					{
						// TODO: Check if controlled unit can swim

						// Check if move target would be in hit 3d hit range
						const float combatRangeSq = ::powf(controlled.GetMeleeReach() + victim->GetMeleeReach(), 2.0f);
						const float distSq = (controlled.GetMover().GetTarget() - victim->GetPosition()).GetSquaredLength();
						if (distSq > combatRangeSq)
						{
							GetAI().Reset();
							return;
						}
					}
				}

				const bool outOfRange =
					controlled.GetSquaredDistanceTo(homePos, false) >= 60.0f * 60.0f ||
					controlled.GetSquaredDistanceTo(controlled.GetMover().GetTarget(), true) >= 60.0f * 60.0f;

				if (GetAsyncTimeMs() >= (m_lastThreatTime + constants::OneSecond * 10) && outOfRange)
				{
					GetAI().Reset();
					return;
				}
			});
		}

		m_entered = true;

		// Save weakptr reference
		std::weak_ptr weakThis = std::static_pointer_cast<CreatureAICombatState>(shared_from_this());

		// Delay first action on the next world tick
		controlled.GetWorldInstance()->GetUniverse().Post([weakThis]() {
			if (const auto strongThis = weakThis.lock())
			{
				strongThis->ChooseNextAction();
			}
		});


		// Raise OnAggro triggers
		controlled.RaiseTrigger(trigger_event::OnAggro, initiator.get());
	}

	void CreatureAICombatState::OnLeave()
	{
		CreatureAIState::OnLeave();

		// Reset all events here to prevent them being fired in another ai state
		m_nextActionCountdown.ended.clear();

		m_nextActionCountdown.Cancel();
		m_onThreatened.disconnect();
		m_getThreat.disconnect();
		m_setThreat.disconnect();
		m_getTopThreatener.disconnect();
		m_onMoveTargetChanged.disconnect();
		m_onUnitStateChanged.disconnect();

		auto& controlled = GetControlled();
		controlled.SetInCombat(false, false);

		// Stop movement!
		controlled.GetMover().StopMovement();

		// All remaining threateners are no longer in combat with this unit
		for (auto& pair : m_threat)
		{
			if (auto threatener = pair.second.threatener.lock())
			{
				threatener->RemoveAttackingUnit(GetControlled());
			}
		}
	}

	void CreatureAICombatState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		GetControlled().AddCombatParticipant(attacker);

		if (attacker.GetTypeId() == ObjectTypeId::Player)
		{
			if (!GetControlled().IsTagged())
			{
				GetControlled().AddLootRecipient(attacker.GetGuid());

				// TODO: Add group members to the list of loot recipients (TODO: We need a party system ^^)
			}
		}
	}

	void CreatureAICombatState::OnCombatMovementChanged()
	{
		CreatureAIState::OnCombatMovementChanged();
	}

	void CreatureAICombatState::OnControlledMoved()
	{
		CreatureAIState::OnControlledMoved();
	}

	void CreatureAICombatState::AddThreat(GameUnitS& threatener, float amount)
	{
		// No negative threat
		amount = std::max(amount, 0.0f);

		// No aggro on dead units
		if (!threatener.IsAlive())
		{
			return;
		}

		// Add threat amount (Note: A value of 0 is fine here, as it will still add an
		// entry to the threat list)
		uint64 guid = threatener.GetGuid();
		auto it = m_threat.find(guid);
		if (it == m_threat.end())
		{
			// Insert new entry
			it = m_threat.insert(m_threat.begin(), std::make_pair(guid, ThreatEntry(threatener, 0.0f)));

			// Watch for unit killed signal
			m_killedSignals[guid] = threatener.killed.connect([this, guid, &threatener](GameUnitS*)
				{
					RemoveThreat(threatener);
				});

			auto strongThreatener = std::static_pointer_cast<GameUnitS>(threatener.shared_from_this());
			std::weak_ptr weakThreatener(strongThreatener);

			// Watch for unit despawned signal
			m_miscSignals[guid] +=
				threatener.despawned.connect([this, strongThreatener](GameObjectS&)
					{
						RemoveThreat(*strongThreatener);
					});

			// Add this unit to the list of attacking units
			threatener.AddAttackingUnit(GetControlled());
			GetControlled().AddCombatParticipant(threatener);
		}

		auto& threatEntry = it->second;
		threatEntry.amount += amount;

		m_lastThreatTime = GetAsyncTimeMs();

		// If not casting right now and already initialized, choose next action
		if (!m_isCasting && m_entered)
		{
			ChooseNextAction();
		}
	}

	void CreatureAICombatState::RemoveThreat(GameUnitS& threatener)
	{
		const uint64 guid = threatener.GetGuid();
		const auto it = m_threat.find(guid);
		if (it != m_threat.end())
		{
			m_threat.erase(it);
		}

		const auto killedIt = m_killedSignals.find(guid);
		if (killedIt != m_killedSignals.end())
		{
			m_killedSignals.erase(killedIt);
		}

		const auto despawnedIt = m_miscSignals.find(guid);
		if (despawnedIt != m_miscSignals.end())
		{
			m_miscSignals.erase(despawnedIt);
		}

		auto& controlled = GetControlled();
		threatener.RemoveAttackingUnit(controlled);

		if (controlled.GetVictim() == &threatener ||
			m_threat.empty())
		{
			controlled.StopAttack();
			controlled.SetTarget(0);
			ChooseNextAction();
		}
	}

	float CreatureAICombatState::GetThreat(const GameUnitS& threatener)
	{
		const uint64 guid = threatener.GetGuid();
		if (const auto it = m_threat.find(guid); it == m_threat.end())
		{
			return 0.0f;
		}
		else
		{
			return it->second.amount;
		}
	}

	void CreatureAICombatState::SetThreat(const GameUnitS& threatener, const float amount)
	{
		const uint64 guid = threatener.GetGuid();
		if (const auto it = m_threat.find(guid); it != m_threat.end())
		{
			it->second.amount = amount;
		}
	}

	GameUnitS* CreatureAICombatState::GetTopThreatener()
	{
		float highestThreat = -1.0f;
		GameUnitS* topThreatener = nullptr;
		for (auto& entry : m_threat)
		{
			if (entry.second.amount > highestThreat)
			{
				topThreatener = entry.second.threatener.lock().get();
				if (topThreatener)
				{
					highestThreat = entry.second.amount;
				}
			}
		}
		return topThreatener;
	}

	void CreatureAICombatState::UpdateVictim()
	{
		GameCreatureS& controlled = GetControlled();

		GameUnitS* victim = controlled.GetVictim();

		// Now, determine the victim with the highest threat value
		float highestThreat = -1.0f;
		GameUnitS* newVictim = nullptr;
		for (auto& entry : m_threat)
		{
			auto threatener = entry.second.threatener.lock();
			if (!threatener)
			{
				continue;
			}

			if (entry.second.amount > highestThreat)
			{
				newVictim = threatener.get();
				highestThreat = entry.second.amount;
			}
		}

		if (newVictim && newVictim != victim)
		{
			controlled.StartAttack(std::static_pointer_cast<GameUnitS>(newVictim->shared_from_this()));
		}
		else if (!newVictim)
		{
			controlled.StopAttack();
			controlled.SetTarget(0);
		}
	}

	void CreatureAICombatState::ChaseTarget(GameUnitS& target)
	{
		// Nothing to do when our unit is rooted
		if (GetControlled().IsRooted())
		{
			return;
		}

		const float combatRange = GetControlled().GetMeleeReach();

		auto& mover = GetControlled().GetMover();

		// Are we moving at all?
		if (!mover.IsMoving())
		{
			// Not moving: Check if we are in hit range to the target
			if (target.GetSquaredDistanceTo(mover.GetCurrentLocation(), true) <= combatRange)
			{
				// We are in combat range, no need to move
				return;
			}
		}
		else
		{
			// Are we on our way to a location where we can hit the target?
			if (target.GetSquaredDistanceTo(mover.GetTarget(), true) <= combatRange)
			{
				// We are, no need to adjust current movement path
				return;
			}
		}

		// Check if target location is too far away from home
		if (m_canReset && target.GetSquaredDistanceTo(GetAI().GetHome().position, false) > 60.0f * 60.0f)
		{
			// We are too far away from home, reset AI
			GetAI().Reset();
			return; // Important: ResetAI might destroy this AI state
		}

		// We need to move!
		if (mover.MoveTo(target.GetPosition(), combatRange * 0.75f))
		{
			// Successfully moving, reset stuck counter
			m_stuckCounter = 0;
			return;
		}

		if (++m_stuckCounter > 20)
		{
			// We are stuck, reset AI
			GetAI().Reset();
			return; // Important: ResetAI might destroy this AI state
		}
	}

	void CreatureAICombatState::ChooseNextAction()
	{
		GameCreatureS& controlled = GetControlled();

		// First, determine our current victim
		UpdateVictim();

		// We should have a valid victim here, otherwise there is nothing to do but to reset
		GameUnitS* victim = controlled.GetVictim();
		if (!victim || !victim->IsAlive())
		{
			// Warning: this will destroy the current AI state.
			GetAI().Reset();
			return;
		}

		m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + 500);
		ChaseTarget(*victim);
	}
}
