// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <algorithm>

#include "game_server/ai/creature_ai_combat_state.h"
#include "game_server/ai/creature_ai.h"
#include "objects/game_creature_s.h"
#include "game_server/world/universe.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// === MovementState Implementation ===

	bool CreatureAICombatState::MovementState::IsValidFor(const GameUnitS& target, float currentCombatRange) const
	{
		if (!isMovingToCombat)
		{
			return false;
		}

		// Check if combat range changed significantly
		if (std::abs(combatRange - currentCombatRange) > 0.5f)
		{
			return false;
		}

		// Check if target is still within reasonable range of our target position
		const float targetDistanceSq = (target.GetPosition() - targetPosition).GetSquaredLength();
		const float toleranceRange = currentCombatRange * 0.5f;
		
		return targetDistanceSq <= (toleranceRange * toleranceRange);
	}

	void CreatureAICombatState::MovementState::UpdateTarget(const Vector3& target, float range)
	{
		targetPosition = target;
		combatRange = range;
		isMovingToCombat = true;
	}

	void CreatureAICombatState::MovementState::Reset()
	{
		targetPosition = Vector3::Zero;
		combatRange = 0.0f;
		isMovingToCombat = false;
	}

	// === CreatureAICombatState Implementation ===
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
		
		// Initialize state
		m_stuckCounter = 0;
		m_movementState.Reset();

		auto& controlled = GetControlled();
		controlled.RemoveAllCombatParticipants();

		// Set initial target
		const std::shared_ptr<GameUnitS> initiator = m_combatInitiator.lock();
		if (initiator)
		{
			AddThreat(*initiator, 0.0f);
		}
		m_combatInitiator.reset();

		// Setup event connections
		SetupEventConnections();

		controlled.SetInCombat(true, false);

		// Setup reset conditions if applicable
		SetupResetConditions();

		m_entered = true;

		// Schedule first action for next tick
		std::weak_ptr weakThis = std::static_pointer_cast<CreatureAICombatState>(shared_from_this());
		controlled.GetWorldInstance()->GetUniverse().Post([weakThis]() 
		{
			if (const auto strongThis = weakThis.lock())
			{
				strongThis->ChooseNextAction();
			}
		});

		// Raise OnAggro triggers
		if (initiator)
		{
			controlled.RaiseTrigger(trigger_event::OnAggro, initiator.get());
		}
	}
	void CreatureAICombatState::OnLeave()
	{
		CreatureAIState::OnLeave();

		// Reset all events and connections
		m_nextActionCountdown.ended.clear();
		m_nextActionCountdown.Cancel();
		
		// Disconnect all event connections
		m_onThreatened.disconnect();
		m_getThreat.disconnect();
		m_setThreat.disconnect();
		m_getTopThreatener.disconnect();
		m_onMoveTargetChanged.disconnect();
		m_onUnitStateChanged.disconnect();

		auto& controlled = GetControlled();
		controlled.SetInCombat(false, false);

		// Stop movement and reset movement state
		controlled.GetMover().StopMovement();
		m_movementState.Reset();

		// Clean up combat participants
		for (auto& pair : m_threat)
		{
			if (auto threatener = pair.second.threatener.lock())
			{
				threatener->RemoveAttackingUnit(GetControlled());
			}
		}

		// Clear all signal containers
		m_killedSignals.clear();
		m_miscSignals.clear();
	}

	void CreatureAICombatState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		GetControlled().AddCombatParticipant(attacker);

		if (attacker.IsPlayer())
		{
			if (!GetControlled().IsTagged())
			{
				GetControlled().AddLootRecipient(attacker.GetGuid());
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

		const uint64 guid = threatener.GetGuid();
		auto it = m_threat.find(guid);
		
		if (it == m_threat.end())
		{
			// Insert new entry
			it = m_threat.emplace(guid, ThreatEntry(threatener, 0.0f)).first;

			// Setup signals for this threatener
			SetupThreatenerSignals(threatener, guid);

			// Add this unit to the list of attacking units
			threatener.AddAttackingUnit(GetControlled());
			GetControlled().AddCombatParticipant(threatener);
		}

		// Update threat amount
		it->second.amount += amount;
		m_lastThreatTime = GetAsyncTimeMs();

		// If not casting and already initialized, choose next action
		if (!m_isCasting && m_entered)
		{
			ChooseNextAction();
		}
	}
	void CreatureAICombatState::RemoveThreat(GameUnitS& threatener)
	{
		const uint64 guid = threatener.GetGuid();
		
		// Remove threat entry
		const auto threatIt = m_threat.find(guid);
		if (threatIt != m_threat.end())
		{
			m_threat.erase(threatIt);
		}

		// Clean up signals
		const auto killedIt = m_killedSignals.find(guid);
		if (killedIt != m_killedSignals.end())
		{
			m_killedSignals.erase(killedIt);
		}

		const auto miscIt = m_miscSignals.find(guid);
		if (miscIt != m_miscSignals.end())
		{
			m_miscSignals.erase(miscIt);
		}

		// Remove combat relationships
		auto& controlled = GetControlled();
		threatener.RemoveAttackingUnit(controlled);

		// Check if we need to find a new victim or reset
		if (controlled.GetVictim() == &threatener || m_threat.empty())
		{
			controlled.StopAttack();
			controlled.SetTarget(0);
			m_movementState.Reset();
			ChooseNextAction();
		}
	}
	float CreatureAICombatState::GetThreat(const GameUnitS& threatener) const
	{
		const uint64 guid = threatener.GetGuid();
		const auto it = m_threat.find(guid);
		return (it != m_threat.end()) ? it->second.amount : 0.0f;
	}

	void CreatureAICombatState::SetThreat(const GameUnitS& threatener, const float amount)
	{
		const uint64 guid = threatener.GetGuid();
		const auto it = m_threat.find(guid);
		if (it != m_threat.end())
		{
			it->second.amount = amount;
		}
	}

	GameUnitS* CreatureAICombatState::GetTopThreatener() const
	{
		float highestThreat = -1.0f;
		GameUnitS* topThreatener = nullptr;
		
		for (const auto& entry : m_threat)
		{
			if (entry.second.amount > highestThreat)
			{
				if (auto threatener = entry.second.threatener.lock())
				{
					topThreatener = threatener.get();
					highestThreat = entry.second.amount;
				}
			}
		}
		
		return topThreatener;
	}

	void CreatureAICombatState::CleanupExpiredThreats()
	{
		std::vector<uint64> expiredGuids;
		
		for (const auto& pair : m_threat)
		{
			if (pair.second.threatener.expired())
			{
				expiredGuids.push_back(pair.first);
			}
		}
		
		for (uint64 guid : expiredGuids)
		{
			m_threat.erase(guid);
			m_killedSignals.erase(guid);
			m_miscSignals.erase(guid);
		}
	}
	void CreatureAICombatState::UpdateVictim()
	{
		// Clean up any expired threat entries first
		CleanupExpiredThreats();

		GameCreatureS& controlled = GetControlled();
		GameUnitS* currentVictim = controlled.GetVictim();

		// Find the unit with the highest threat
		GameUnitS* newVictim = GetTopThreatener();

		// Only switch victims if necessary
		if (newVictim && newVictim != currentVictim)
		{
			if (!newVictim->CanBeSeenBy(controlled))
			{
				controlled.StopAttack();
				controlled.SetTarget(0);
				m_movementState.Reset();
			}
			else
			{
				controlled.StartAttack(std::static_pointer_cast<GameUnitS>(newVictim->shared_from_this()));
				m_movementState.Reset(); // Reset movement when switching targets
			}
		}
		else if (!newVictim)
		{
			controlled.StopAttack();
			controlled.SetTarget(0);
			m_movementState.Reset();
		}
	}
	bool CreatureAICombatState::ShouldMoveToTarget(const GameUnitS& target) const
	{
		auto& controlled = GetControlled();
		auto& mover = controlled.GetMover();

		// Nothing to do when rooted
		if (controlled.IsRooted())
		{
			return false;
		}

		const float combatRange = controlled.GetMeleeReach() + target.GetMeleeReach();
		const float combatRangeSq = combatRange * combatRange;

		// Check if we're already in combat range
		const float currentDistanceSq = target.GetSquaredDistanceTo(mover.GetCurrentLocation(), true);
		if (currentDistanceSq <= combatRangeSq)
		{
			return false; // Already in range
		}

		// If we're moving, check if our current movement is still valid
		if (mover.IsMoving())
		{
			// Check if current movement path is still valid for this target
			if (m_movementState.IsValidFor(target, combatRange))
			{
				return false; // Current movement is still good
			}
		}

		return true; // Need to initiate or update movement
	}

	bool CreatureAICombatState::ChaseTarget(GameUnitS& target)
	{
		if (!ShouldMoveToTarget(target))
		{
			return true; // No movement needed
		}

		// Check if target is too far from home
		if (ShouldResetAI(&target))
		{
			GetAI().Reset();
			return false; // AI reset, movement aborted
		}

		const float combatRange = GetControlled().GetMeleeReach() + target.GetMeleeReach();
		const float moveRange = combatRange * COMBAT_RANGE_FACTOR;

		auto& mover = GetControlled().GetMover();
		
		// Attempt to move to the target
		if (mover.MoveTo(target.GetPosition(), moveRange))
		{
			// Successfully initiated movement
			m_movementState.UpdateTarget(target.GetPosition(), combatRange);
			m_stuckCounter = 0;
			return true;
		}

		// Movement failed, handle stuck detection
		return !HandleMovementFailure();
	}

	bool CreatureAICombatState::HandleMovementFailure()
	{
		if (++m_stuckCounter > MAX_STUCK_COUNT)
		{
			// We are stuck, reset AI
			GetAI().Reset();
			return true; // Indicates AI was reset
		}
		
		return false; // Not stuck yet
	}
	void CreatureAICombatState::ChooseNextAction()
	{
		GameCreatureS& controlled = GetControlled();

		// First, determine our current victim
		UpdateVictim();

		// Check if we should reset due to no valid targets
		GameUnitS* victim = controlled.GetVictim();
		if (!victim || !victim->IsAlive())
		{
			GetAI().Reset();
			return;
		}

		// Schedule next action check
		m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + ACTION_INTERVAL_MS);

		// Attempt to chase the target (only moves if necessary)
		ChaseTarget(*victim);
	}

	bool CreatureAICombatState::ShouldResetAI(const GameUnitS* victim) const
	{
		if (!m_canReset)
		{
			return false;
		}

		const auto& controlled = GetControlled();
		const auto& homePos = GetAI().GetHome().position;

		// Check distance constraints
		const bool outOfRange = 
			controlled.GetSquaredDistanceTo(homePos, false) >= RESET_DISTANCE_SQ ||
			(victim && victim->GetSquaredDistanceTo(homePos, false) >= RESET_DISTANCE_SQ);

		// Check timeout since last threat
		const bool timedOut = 
			GetAsyncTimeMs() >= (m_lastThreatTime + RESET_TIMEOUT_MS);

		return outOfRange && timedOut;
	}

	void CreatureAICombatState::SetupEventConnections()
	{
		auto& controlled = GetControlled();

		// Setup action countdown
		m_nextActionCountdown.ended.connect(this, &CreatureAICombatState::ChooseNextAction);

		// Watch for threat events
		m_onThreatened = controlled.threatened.connect([this](GameUnitS& threatener, float amount)
		{
			AddThreat(threatener, amount);
		});
	}

	void CreatureAICombatState::SetupResetConditions()
	{
		auto& controlled = GetControlled();
		
		ASSERT(controlled.GetWorldInstance());

		// Only setup reset conditions in non-instanced PvE areas
		if (!controlled.GetWorldInstance()->IsInstancedPvE())
		{
			m_canReset = true;

			m_onMoveTargetChanged = controlled.GetMover().targetChanged.connect([this]()
			{
				const auto& controlled = GetControlled();
				
				if (auto* victim = controlled.GetVictim())
				{
					// Handle flying/swimming targets
					if (victim->GetMovementInfo().movementFlags & (movement_flags::Flying | movement_flags::Swimming))
					{
						// TODO: Check if controlled unit can swim/fly
						const float combatRangeSq = ::powf(controlled.GetMeleeReach() + victim->GetMeleeReach(), 2.0f);
						const float distSq = (controlled.GetMover().GetTarget() - victim->GetPosition()).GetSquaredLength();
						
						if (distSq > combatRangeSq)
						{
							GetAI().Reset();
							return;
						}
					}

					// Check if we should reset due to distance/timeout
					if (ShouldResetAI(victim))
					{
						GetAI().Reset();
						return;
					}
				}
			});
		}
	}

	void CreatureAICombatState::SetupThreatenerSignals(GameUnitS& threatener, uint64 guid)
	{
		// Watch for unit killed signal
		m_killedSignals[guid] = threatener.killed.connect([this, guid, &threatener](GameUnitS*)
		{
			RemoveThreat(threatener);
		});

		// Watch for unit despawned signal
		auto strongThreatener = std::static_pointer_cast<GameUnitS>(threatener.shared_from_this());
		m_miscSignals[guid] += threatener.despawned.connect([this, strongThreatener](GameObjectS&)
		{
			RemoveThreat(*strongThreatener);
		});
	}
}
