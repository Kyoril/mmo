// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <algorithm>
#include <cmath>

#include "game_server/ai/creature_ai_combat_state.h"
#include "game_server/ai/creature_ai.h"
#include "objects/game_creature_s.h"
#include "game_server/world/universe.h"
#include "log/default_log_levels.h"

namespace mmo
{
	// Math constants
	static constexpr float PI = 3.14159265359f;

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
		shouldCheckFanning = false; // Don't check fanning until we reach the initial position
		hasReachedInitialPosition = false; // Reset this flag for new movement
	}

	void CreatureAICombatState::MovementState::Reset()
	{
		targetPosition = Vector3::Zero;
		combatRange = 0.0f;
		isMovingToCombat = false;
		shouldCheckFanning = false;
		hasReachedInitialPosition = false;
		nextFanningCheckTime = 0;
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

		// Use the actual attack range for consistency with OnAttackSwing()
		const float attackRange = controlled.GetMeleeReach() + target.GetMeleeReach();
		const float attackRangeSq = attackRange * attackRange;

		// Check if we're already in attack range
		const float currentDistanceSq = target.GetSquaredDistanceTo(mover.GetCurrentLocation(), true);
		if (currentDistanceSq <= attackRangeSq)
		{
			return false; // Already in range
		}

		// If we're moving, check if we'll be in range by the time we reach our destination
		if (mover.IsMoving())
		{
			// Check if current movement path is still valid for this target
			if (m_movementState.IsValidFor(target, attackRange))
			{
				// Additional check: if target is moving toward us, we might intercept before reaching destination
				const Vector3 ourDestination = mover.GetTarget();
				const float distanceToDestinationSq = target.GetSquaredDistanceTo(ourDestination, true);
				
				// If target is close to where we're going, continue current movement
				if (distanceToDestinationSq <= attackRangeSq)
				{
					return false; // Current movement should get us in range
				}
			}
		}

		return true; // Need to initiate or update movement
	}

	Vector3 CreatureAICombatState::PredictTargetPosition(const GameUnitS& target) const
	{
		Vector3 predictedPosition = target.GetPosition();
		
		// If target is moving, predict where they'll be
		const auto& targetMover = target.GetMover();
		if (targetMover.IsMoving())
		{
			const Vector3 currentPos = target.GetPosition();
			const Vector3 targetDestination = targetMover.GetTarget();
			const Vector3 direction = (targetDestination - currentPos).NormalizedCopy();
			
			// Predict 1-2 seconds ahead based on target's movement speed
			const float targetSpeed = target.GetSpeed(movement_type::Run);
			const float predictionTime = 1.5f; // seconds
			const float maxPredictionDistance = targetSpeed * predictionTime;
			
			// Don't predict beyond their actual destination
			const float distanceToDestination = (targetDestination - currentPos).GetLength();
			const float actualPredictionDistance = std::min(maxPredictionDistance, distanceToDestination);
			
			predictedPosition = currentPos + direction * actualPredictionDistance;
		}
		
		return predictedPosition;
	}

	Vector3 CreatureAICombatState::CalculateFanningPosition(const GameUnitS& target, const Vector3& basePosition) const
	{
		auto& controlled = GetControlled();
		
		// Get all units attacking the same target within a reasonable radius
		std::vector<GameUnitS*> nearbyAttackers;
		const Vector3& targetPos = target.GetPosition();
		
		// Find nearby units that are also attacking our target
		controlled.GetWorldInstance()->GetUnitFinder().FindUnits(
			Circle(targetPos.x, targetPos.z, FANNING_SEARCH_RADIUS), 
			[&](GameUnitS& unit) -> bool
			{
				// Skip ourselves
				if (&unit == &controlled)
				{
					return true;
				}
				
				// Only consider creatures (not players)
				if (unit.GetTypeId() != ObjectTypeId::Unit)
				{
					return true;
				}
				
				// Only consider units that are attacking the same target
				if (unit.GetVictim() != &target)
				{
					return true;
				}
				
				// Only consider units that are close enough to matter
				const float distanceToTarget = unit.GetSquaredDistanceTo(targetPos, false);
				const float maxReach = unit.GetMeleeReach() + target.GetMeleeReach() + FANNING_MIN_DISTANCE;
				if (distanceToTarget > (maxReach * maxReach))
				{
					return true;
				}
				
				nearbyAttackers.push_back(&unit);
				return true;
			});
		
		// If no nearby attackers, no need to fan out
		if (nearbyAttackers.empty())
		{
			return basePosition;
		}
		
		// Calculate our desired radius from the target
		const float attackRange = controlled.GetMeleeReach() + target.GetMeleeReach();
		const float desiredRadius = attackRange * COMBAT_RANGE_FACTOR;
		
		// Use a more deterministic approach: sort by GUID for consistent positioning
		std::sort(nearbyAttackers.begin(), nearbyAttackers.end(), 
			[](const GameUnitS* a, const GameUnitS* b) 
			{
				return a->GetGuid() < b->GetGuid();
			});
		
		// Find our index in the sorted list to determine our base angle
		int ourIndex = 0;
		const uint64 ourGuid = controlled.GetGuid();
		for (size_t i = 0; i < nearbyAttackers.size(); ++i)
		{
			if (nearbyAttackers[i]->GetGuid() < ourGuid)
			{
				ourIndex++;
			}
		}
		
		// Total number of attackers including ourselves
		const int totalAttackers = static_cast<int>(nearbyAttackers.size()) + 1;
		
		// Calculate base angle for even distribution
		const float baseAngleStep = (2.0f * PI) / static_cast<float>(totalAttackers);
		float ourAngle = static_cast<float>(ourIndex) * baseAngleStep;
		
		// Check if any nearby creature is too close to our calculated position
		// If so, try adjacent positions
		Vector3 bestPosition;
		float bestDistance = 0.0f;
		bool foundGoodPosition = false;
		
		for (int attempt = 0; attempt < totalAttackers && !foundGoodPosition; ++attempt)
		{
			const float testAngle = ourAngle + (static_cast<float>(attempt) * baseAngleStep);
			
			const float x = targetPos.x + desiredRadius * cos(testAngle);
			const float z = targetPos.z + desiredRadius * sin(testAngle);
			const Vector3 testPosition(x, targetPos.y, z);
			
			// Check if this position is far enough from other attackers
			float minDistanceToOthers = std::numeric_limits<float>::max();
			for (const GameUnitS* other : nearbyAttackers)
			{
				const float distanceToOther = other->GetSquaredDistanceTo(testPosition, false);
				minDistanceToOthers = std::min(minDistanceToOthers, distanceToOther);
			}
			
			// Accept this position if it's far enough from others or if it's better than what we found
			const float minRequiredDistance = FANNING_MIN_DISTANCE * FANNING_MIN_DISTANCE;
			if (minDistanceToOthers >= minRequiredDistance)
			{
				bestPosition = testPosition;
				foundGoodPosition = true;
			}
			else if (minDistanceToOthers > bestDistance)
			{
				bestPosition = testPosition;
				bestDistance = minDistanceToOthers;
			}
		}
		
		// If we couldn't find a good position, use the best one we found
		if (!foundGoodPosition && bestDistance > 0.0f)
		{
			return bestPosition;
		}
		else if (foundGoodPosition)
		{
			return bestPosition;
		}
		
		// Fallback: use the original calculated position
		const float x = targetPos.x + desiredRadius * cos(ourAngle);
		const float z = targetPos.z + desiredRadius * sin(ourAngle);
		return Vector3(x, targetPos.y, z);
	}

	bool CreatureAICombatState::NeedsFanningAdjustment(const GameUnitS& target) const
	{
		auto& controlled = GetControlled();
		
		// Only check if we have reached our initial position and are supposed to check fanning
		if (!m_movementState.hasReachedInitialPosition || !m_movementState.shouldCheckFanning)
		{
			return false;
		}
		
		// Check if enough time has passed since the last fanning check
		const GameTime currentTime = GetAsyncTimeMs();
		if (currentTime < m_movementState.nextFanningCheckTime)
		{
			return false;
		}
		
		// Only check if we're already in combat range
		const float attackRange = controlled.GetMeleeReach() + target.GetMeleeReach();
		const float currentDistanceSq = target.GetSquaredDistanceTo(controlled.GetPosition(), true);
		if (currentDistanceSq > (attackRange * attackRange))
		{
			return false; // Not in range yet
		}
		
		// Count nearby attackers attacking the same target
		int nearbyCount = 0;
		const Vector3& targetPos = target.GetPosition();
		
		controlled.GetWorldInstance()->GetUnitFinder().FindUnits(
			Circle(targetPos.x, targetPos.z, FANNING_SEARCH_RADIUS), 
			[&](GameUnitS& unit) -> bool
			{
				// Skip ourselves
				if (&unit == &controlled)
				{
					return true;
				}
				
				// Only consider creatures (not players)
				if (unit.GetTypeId() != ObjectTypeId::Unit)
				{
					return true;
				}
				
				// Only consider units that are attacking the same target
				if (unit.GetVictim() != &target)
				{
					return true;
				}
				
				// Check if they're too close to us
				const float distanceToUs = unit.GetSquaredDistanceTo(controlled.GetPosition(), false);
				if (distanceToUs < (FANNING_MIN_DISTANCE * FANNING_MIN_DISTANCE))
				{
					nearbyCount++;
				}
				
				return true;
			});
		
		// Need fanning if there are multiple creatures too close
		return nearbyCount > 0;
	}

	bool CreatureAICombatState::ChaseTarget(GameUnitS& target)
	{
		auto& controlled = GetControlled();
		auto& mover = controlled.GetMover();
		const GameTime currentTime = GetAsyncTimeMs();
		
		// Check if we need fanning adjustment when already in range
		if (!ShouldMoveToTarget(target))
		{
			// We're already in range - mark that we've reached our initial position
			if (!m_movementState.hasReachedInitialPosition)
			{
				m_movementState.hasReachedInitialPosition = true;
				m_movementState.shouldCheckFanning = true; // Now enable fanning checks
				// Set the next fanning check time to give the creature time to settle
				m_movementState.nextFanningCheckTime = currentTime + FANNING_CHECK_INTERVAL;
			}
			else if (currentTime >= m_movementState.nextFanningCheckTime && !m_movementState.shouldCheckFanning)
			{
				// Re-enable fanning checks periodically
				m_movementState.shouldCheckFanning = true;
			}
			
			// Check if we need fanning adjustment
			if (NeedsFanningAdjustment(target))
			{
				const Vector3 currentPos = controlled.GetPosition();
				const Vector3 fannedPosition = CalculateFanningPosition(target, currentPos);
				
				// Only move if the fanned position is significantly different
				const float adjustmentDistance = (fannedPosition - currentPos).GetLength();
				if (adjustmentDistance > 1.0f) // Only adjust if we need to move more than 1 unit
				{
					const float attackRange = controlled.GetMeleeReach() + target.GetMeleeReach();
					const float moveRange = attackRange * COMBAT_RANGE_FACTOR;

					DLOG("Adjust fanning position to " << fannedPosition << " (range: " << moveRange << ")");
					if (mover.MoveTo(fannedPosition, moveRange))
					{
						m_movementState.UpdateTarget(fannedPosition, attackRange);
						// Mark as having reached initial position since this is a fanning adjustment
						m_movementState.hasReachedInitialPosition = true;
						m_movementState.shouldCheckFanning = false; // Don't check again immediately
						m_movementState.nextFanningCheckTime = currentTime + FANNING_CHECK_INTERVAL;
						m_stuckCounter = 0;
					}
				}
				else
				{
					// We're good where we are, disable further fanning checks for a while
					m_movementState.shouldCheckFanning = false;
					m_movementState.nextFanningCheckTime = currentTime + FANNING_CHECK_INTERVAL;
				}
			}

			return true; // No initial movement needed
		}

		// Check if target is too far from home
		if (ShouldResetAI(&target))
		{
			GetAI().Reset();
			return false; // AI reset, movement aborted
		}

		// Use consistent range calculation with ShouldMoveToTarget and OnAttackSwing
		const float attackRange = controlled.GetMeleeReach() + target.GetMeleeReach();
		// Move slightly closer than attack range to ensure we're definitely in range
		const float moveRange = attackRange * COMBAT_RANGE_FACTOR;
		
		// Use predicted target position for better interception (no fanning yet)
		const Vector3 targetPosition = PredictTargetPosition(target);
		
		// Attempt to move to the predicted target position
		if (mover.MoveTo(targetPosition, moveRange))
		{
			// Successfully initiated movement
			m_movementState.UpdateTarget(targetPosition, attackRange);
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

		// Use shorter intervals when target is moving to improve responsiveness
		uint32 actionInterval = ACTION_INTERVAL_MS;
		if (victim->GetMover().IsMoving())
		{
			actionInterval = ACTION_INTERVAL_MS / 2; // 250ms instead of 500ms for moving targets
		}

		// Schedule next action check
		m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + actionInterval);

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
					// Immediate range check when we start moving - if we're already in range, stop
					const float attackRange = controlled.GetMeleeReach() + victim->GetMeleeReach();
					const float attackRangeSq = attackRange * attackRange;
					const float currentDistanceSq = victim->GetSquaredDistanceTo(controlled.GetMover().GetCurrentLocation(), true);
					
					if (currentDistanceSq <= attackRangeSq)
					{
						// We're already in range, stop movement
						controlled.GetMover().StopMovement();
						return;
					}
					
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
