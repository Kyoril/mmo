// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include <algorithm>
#include <cmath>
#include <limits>

#include "game_server/ai/creature_ai_combat_state.h"
#include "game_server/ai/creature_ai.h"
#include "game_server/ai/creature_combat_script.h"
#include "game_server/ai/creature_combat_script_registry.h"
#include "game_server/ai/creature_separation_manager.h"
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
		lastWaypointTarget = target;
	}

	void CreatureAICombatState::MovementState::Reset()
	{
		targetPosition = Vector3::Zero;
		combatRange = 0.0f;
		isMovingToCombat = false;
		lastWaypointTarget = Vector3::Zero;
	}

	// === CreatureAICombatState Implementation ===
	CreatureAICombatState::CreatureAICombatState(CreatureAI& ai, GameUnitS& victim)
		: CreatureAIState(ai)
		, m_combatInitiator(std::static_pointer_cast<GameUnitS>(victim.shared_from_this()))
		, m_combatBehavior(CombatBehavior::Melee)
		, m_lastSpellCastTime(0)
		, m_lastThreatTime(0)
		, m_stuckCounter(0)
		, m_nextActionCountdown(ai.GetControlled().GetTimers())
		, m_recalculationCountdown(ai.GetControlled().GetTimers())
		, m_isCasting(false)
		, m_entered(false)
		, m_isRanged(false)
		, m_canReset(false)
		, m_castingTimeoutEnd(0)
	{
	}

	CreatureAICombatState::~CreatureAICombatState()
	{
	}
	void CreatureAICombatState::OnEnter()
	{
		CreatureAIState::OnEnter();
		GetControlled().SetMovementMode(unit_movement_mode::Run);
		
		// Initialize state
		m_stuckCounter = 0;
		m_movementState.Reset();
		m_lastSpellCastTime = 0;

		// Initialize recalculation countdown to fire at 500ms intervals
		m_recalculationCountdown.SetEnd(GetAsyncTimeMs() + RECALCULATION_INTERVAL_MS);

		auto& controlled = GetControlled();
		controlled.RemoveAllCombatParticipants();

		// Initialize available spells from creature entry
		InitializeSpells();
		
		// Determine combat behavior based on spells and creature type
		m_combatBehavior = DetermineCombatBehavior();

		// Set initial target
		const std::shared_ptr<GameUnitS> initiator = m_combatInitiator.lock();
		if (initiator)
		{
			AddThreat(*initiator, 0.0f);

			// Register the combat initiator as a loot recipient immediately on combat
			// entry. This ensures that if the creature dies in the same frame it enters
			// combat (i.e. a one-shot from idle), the death state still finds a tagged
			// creature and distributes XP and loot correctly.
			if (initiator->IsPlayer() && !controlled.IsTagged())
			{
				controlled.AddLootRecipient(initiator->GetGuid());
			}
		}
		m_combatInitiator.reset();

		// Setup event connections
		SetupEventConnections();

		controlled.SetInCombat(true, false);

		// Setup reset conditions if applicable
		SetupResetConditions();

		m_entered = true;

		// Create combat script if one is configured for this creature
		const auto& scriptName = controlled.GetEntry().script_name();
		if (!scriptName.empty())
		{
			m_script = CreatureCombatScriptRegistry::Instance().CreateScript(scriptName, *this);
			if (!m_script)
			{
				WLOG("Creature " << controlled.GetEntry().id() << " has script_name '" << scriptName << "' but no script is registered with that name");
			}
		}

		// Notify script that combat has started
		if (m_script)
		{
			m_script->OnCombatStart();
		}

		// Schedule first action for next tick
		std::weak_ptr weakThis = std::static_pointer_cast<CreatureAICombatState>(shared_from_this());
		controlled.GetWorldInstance()->GetUniverse().Post([weakThis]() 
		{
			if (const auto strongThis = weakThis.lock())
			{
				strongThis->ChooseNextAction();
			}
		});

		// Start regeneration in combat
		controlled.StartRegeneration();

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
		m_onSpellCastStarted.disconnect();

		auto& controlled = GetControlled();
		controlled.SetInCombat(false, false);

		// Stop regeneration out of combat
		controlled.StopRegeneration();

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

		// Notify script and clean up
		if (m_script)
		{
			m_script->OnCombatEnd();
			m_script.reset();
		}
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

		// Notify script and check health thresholds
		if (m_script)
		{
			m_script->OnDamageTaken(attacker);
			m_script->CheckHealthThresholds();
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

	/**
	 * @brief Handles the completion of a spell cast.
	 * 
	 * This method is called when a spell casting operation completes, either
	 * successfully or with failure. It performs the following actions:
	 * - Resets the internal casting state
	 * - For failed caster spells with low mana, considers temporary melee mode
	 * - Schedules the next combat action immediately
	 * 
	 * @param succeeded Whether the spell cast completed successfully.
	 */
	void CreatureAICombatState::OnSpellCastEnded(bool succeeded)
	{
		// Clear casting state
		m_isCasting = false;
		m_castingTimeoutEnd = 0;
		
		// If spell casting failed and we're a caster with no mana, consider switching to melee
		if (!succeeded && m_combatBehavior == CombatBehavior::Caster)
		{
			const auto& controlled = GetControlled();
			if (controlled.GetPower() < 10) // Very low mana threshold
			{
				// Temporarily switch to melee mode until mana recovers
				// This could be expanded with a more sophisticated state system
			}
		}

		// Notify the script
		if (m_script)
		{
			m_script->OnSpellCastEnded(succeeded);
		}
		
		// Schedule next action immediately after spell cast ends
		if (m_entered)
		{
			ChooseNextAction();
		}
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

		// Root fallback: if rooted and top target is out of melee reach, find nearest in-range alternative.
		if (newVictim && controlled.IsRooted())
		{
			const float reach = controlled.GetMeleeReach() + newVictim->GetMeleeReach();
			const float dist = (controlled.GetPosition() - newVictim->GetPosition()).GetLength();
			if (dist > reach)
			{
				// Scan threat list for the closest unit within melee reach.
				GameUnitS* fallback = nullptr;
				float bestDist = std::numeric_limits<float>::max();
				for (const auto& pair : m_threat)
				{
					GameUnitS* candidate = pair.second.threatener.lock().get();
					if (!candidate || !candidate->IsAlive()) continue;
					const float r = controlled.GetMeleeReach() + candidate->GetMeleeReach();
					const float d = (controlled.GetPosition() - candidate->GetPosition()).GetLength();
					if (d <= r && d < bestDist)
					{
						bestDist = d;
						fallback = candidate;
					}
				}

				if (fallback != nullptr)
				{
					// In-range fallback found — attack it.
					newVictim = fallback;
				}
				else
				{
					// No target in reach while rooted — clear the visual target so the unit
					// doesn't appear to be targeting anyone.  Keep newVictim non-null so we
					// don't abort combat; just idle until something walks in range.
					controlled.SetTarget(0);
					// Don't call StartAttack — nothing to attack right now.
					return;
				}
			}
		}

		// Only switch victims if necessary
		if (newVictim && newVictim != currentVictim)
		{
			if (!newVictim->CanBeSeenBy(controlled))
			{
				// Unit is invisible/stealthed - drop from threat list so the creature
				// can reset if no other valid targets remain.
				RemoveThreat(*newVictim);
			}
			else
			{
				controlled.StartAttack(std::static_pointer_cast<GameUnitS>(newVictim->shared_from_this()));
				m_movementState.Reset(); // Reset movement when switching targets
			}
		}
		else if (newVictim && newVictim == currentVictim)
		{
			// Existing victim — check if they've gone invisible/stealthed mid-combat.
			if (!newVictim->CanBeSeenBy(controlled))
			{
				RemoveThreat(*newVictim);
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

		// Nothing to do when rooted, under forced movement (fear/disorient), or casting
		if (controlled.IsRooted() || controlled.IsUnderForcedMovement() || m_isCasting)
		{
			return false;
		}

		// Use the actual attack range for consistency with OnAttackSwing() — 2D (flat) distance,
		// matching the check in OnAttackSwing which uses withHeight=false.
		const float attackRange = controlled.GetMeleeReach() + target.GetMeleeReach();
		const float attackRangeSq = attackRange * attackRange;

		// Check if we're already in attack range (flat/2D, matching OnAttackSwing)
		const float currentDistanceSq = target.GetSquaredDistanceTo(mover.GetCurrentLocation(), false);
		if (currentDistanceSq <= attackRangeSq)
		{
			return false; // Already in range
		}

		// Check if player has moved more than threshold distance from our current waypoint target
		// This triggers recalculation when the player moves significantly (e.g., kiting away)
		if (m_movementState.isMovingToCombat)
		{
			const float playerDistanceFromWaypointSq = target.GetSquaredDistanceTo(m_movementState.lastWaypointTarget, false);
			if (playerDistanceFromWaypointSq > PLAYER_POSITION_THRESHOLD)
			{
				DLOG("Recalculation: distance_sq=" << playerDistanceFromWaypointSq << " > threshold; triggering waypoint recompute");
				return true; // Player moved too far, need to recalculate
			}
		}

		// If we're moving, check if we'll be in range by the time we reach our destination
		if (mover.IsMoving())
		{
			// Check if current movement path is still valid for this target
			if (m_movementState.IsValidFor(target, attackRange))
			{
				// Additional check: if target is moving toward us, we might intercept before reaching destination
				const Vector3 ourDestination = mover.GetTarget();
				const float distanceToDestinationSq = target.GetSquaredDistanceTo(ourDestination, false);
				
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

	bool CreatureAICombatState::ChaseTarget(GameUnitS& target)
	{
		// Don't move while casting or under forced movement (fear/disorient controller)
		if (m_isCasting || GetControlled().IsUnderForcedMovement())
		{
			return true; // Consider this successful - we're doing what we should be doing
		}

		// Check if the script prevents movement
		if (m_script && !m_script->CanMove())
		{
			return true; // Script says we can't move, treat as success
		}

		// Also update waypoint recalculation check to use flat distance (matching OnAttackSwing)
		if (m_movementState.isMovingToCombat && !GetControlled().IsRooted())
		{
			const float playerDistanceFromWaypointSq = target.GetSquaredDistanceTo(m_movementState.lastWaypointTarget, false);
			if (playerDistanceFromWaypointSq > PLAYER_POSITION_THRESHOLD)
			{
				DLOG("Recalculation: distance_sq=" << playerDistanceFromWaypointSq << " > threshold; triggering waypoint recompute");
				// Invalidate movement state to force recalculation
				m_movementState.Reset();
			}
		}

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

		// Use consistent range calculation with ShouldMoveToTarget and OnAttackSwing.
		// Stop just inside attack range (COMBAT_RANGE_FACTOR keeps the creature from
		// standing inside the target while ensuring it's within auto-attack range).
		const float attackRange = GetControlled().GetMeleeReach() + target.GetMeleeReach();
		const float moveRange = attackRange * COMBAT_RANGE_FACTOR;

		auto& mover = GetControlled().GetMover();
		
		// Use predicted target position for better interception
		Vector3 targetPosition = PredictTargetPosition(target);

		// Apply formation offset so multiple creatures spread around the target
		// instead of all stacking in the same spot
		targetPosition = CalculateFormationPosition(target, targetPosition, moveRange);
		
		// Attempt to move to the formation-adjusted target position
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

	/**
	 * @brief The core decision-making method for creature combat actions.
	 * 
	 * This method implements the main AI logic for different combat behaviors:
	 * 
	 * 1. **Spell Casting Check**: If currently casting, wait for completion with timeout
	 * 2. **Target Validation**: Ensure we have a valid, alive victim
	 * 3. **Spell Prioritization**: For casters/ranged, prioritize spell casting over melee
	 * 4. **Range Management**: Move to optimal range based on behavior type
	 * 5. **Fallback Logic**: Fall back to melee when spells unavailable
	 * 
	 * The method respects the creature's combat behavior:
	 * - **Melee**: Primarily chases targets for auto-attacks
	 * - **Caster/Ranged**: Maintains distance and prioritizes spell casting
	 * 
	 * Action intervals are dynamically adjusted based on target movement.
	 */
	void CreatureAICombatState::ChooseNextAction()
	{
		GameCreatureS& controlled = GetControlled();

		// If we're currently casting a spell, wait for it to complete
		if (m_isCasting)
		{
			// Check if enough time has passed since we started casting using our timeout
			const auto currentTime = GetAsyncTimeMs();
			if (currentTime > m_castingTimeoutEnd)
			{
				// Spell casting has taken too long, assume it's finished.
				// OnSpellCastEnded will recursively call ChooseNextAction after
				// clearing the casting state. We must return here to avoid
				// continuing on a potentially destroyed 'this' (the recursive
				// call may trigger Reset which destroys this combat state).
				OnSpellCastEnded(false);
				return;
			}
			else
			{
				// Schedule next check sooner during casting
				m_nextActionCountdown.SetEnd(currentTime + 250);
				return;
			}
		}

		// If a combat script is active and handles actions completely,
		// we skip UpdateVictim to avoid re-starting auto-attack each tick.
		if (m_script)
		{
			// Clean up expired threats so the threat list stays valid
			CleanupExpiredThreats();

			// Check if there are any threateners left
			if (m_threat.empty())
			{
				GetAI().Reset();
				return;
			}

			// Schedule next action check
			m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + ACTION_INTERVAL_MS);

			// Check for script-defined reset conditions
			if (m_script->ShouldResetCombat())
			{
				GetAI().Reset();
				return;
			}

			// Let the script choose the next action
			if (m_script->OnChooseAction())
			{
				return; // Script handled the action entirely
			}

			// Script returned false — fall through to default AI below.
			// UpdateVictim is needed for the default AI path.
		}

		// Fear suppression: CC controller drives movement; suppress all attacks.
		if (controlled.IsFeared())
		{
			controlled.StopAttack();
			m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + ACTION_INTERVAL_MS);
			return;
		}

		// Sleep/stun suppression: unit cannot act; keep the countdown alive so
		// combat resumes naturally on the next tick after the CC expires.
		if (controlled.IsSleeping() || controlled.IsStunned())
		{
			controlled.StopAttack();
			m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + ACTION_INTERVAL_MS);
			return;
		}

		// Root suppression: unit can attack in melee but cannot chase.
		// If the threat list is non-empty, keep the countdown alive and let
		// UpdateVictim pick an in-range target (or idle) rather than resetting.
		if (controlled.IsRooted() && !m_threat.empty())
		{
			UpdateVictim();
			if (const GameUnitS* rootedVictim = controlled.GetVictim())
			{
				controlled.SetFacing(controlled.GetAngle(*rootedVictim));
			}
			m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + ACTION_INTERVAL_MS);
			return;
		}

		// Determine our current victim (also starts auto-attack on new targets)
		UpdateVictim();

		// Check if we should reset due to no valid targets
		GameUnitS* victim = controlled.GetVictim();
		if (!victim || !victim->IsAlive())
		{
			// If the threat list is still non-empty, don't reset immediately — perhaps
			// the target briefly became unreachable or invisible.  Reschedule and try
			// again on the next tick so transient conditions don't abort combat.
			if (!m_threat.empty())
			{
				m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + ACTION_INTERVAL_MS);
				return;
			}
			GetAI().Reset();
			return;
		}

		// Face the victim on the server side (no network update needed; client renders creature facing automatically)
		controlled.SetFacing(controlled.GetAngle(*victim));

		// Update spell cooldowns
		UpdateSpellCooldowns();

		// Check if periodic recalculation countdown has fired
		const auto currentTime = GetAsyncTimeMs();
		if (currentTime >= m_recalculationCountdown.GetEnd())
		{
			// Force movement revalidation by resetting movement state
			// This will trigger a fresh MoveToOptimalRange() calculation
			m_movementState.Reset();
			
			// Reset countdown for next interval
			m_recalculationCountdown.SetEnd(currentTime + RECALCULATION_INTERVAL_MS);
		}

		// Use shorter intervals when target is moving to improve responsiveness
		uint32 actionInterval = ACTION_INTERVAL_MS;
		if (victim->GetMover().IsMoving())
		{
			actionInterval = ACTION_INTERVAL_MS / 2; // 250ms instead of 500ms for moving targets
		}

		// Schedule next action check
		m_nextActionCountdown.SetEnd(GetAsyncTimeMs() + actionInterval);

		// === Default AI logic (runs when no script, or script returned false) ===

		// For casters and ranged units, prioritize spell casting if possible
		if ((m_combatBehavior == CombatBehavior::Caster || m_combatBehavior == CombatBehavior::Ranged) && 
			CanCastSpells())
		{
			// Try to cast a spell first
			const CreatureSpell* bestSpell = SelectBestSpell(*victim);
			if (bestSpell)
			{
				// Check if we're in range for the spell
				if (IsInOptimalRange(*victim))
				{
					// Cast the spell
					if (CastSpell(*bestSpell, *victim))
					{
						return; // Spell casting initiated, wait for next action
					}
				}
				else
				{
					// Move to optimal range for spell casting (only if not casting)
					if (!m_isCasting && !MoveToOptimalRange(*victim))
					{
						// Movement failed, fallback to melee if possible
						if (m_combatBehavior == CombatBehavior::Caster)
						{
							ChaseTarget(*victim); // Emergency melee mode for casters
						}
					}
					return;
				}
			}
		}

		// For melee units or when spells aren't available, use melee combat (only if not casting)
		if (!m_isCasting && (m_combatBehavior == CombatBehavior::Melee || !CanCastSpells()))
		{
			// Attempt to chase the target (only moves if necessary)
			ChaseTarget(*victim);
		}
		else if (!m_isCasting)
		{
			// Caster with no available spells - move to optimal range and wait
			if (!IsInOptimalRange(*victim))
			{
				MoveToOptimalRange(*victim);
			}
		}
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

		// Watch for spell casting events to properly track casting state
		m_onSpellCastStarted = controlled.startedCasting.connect([this](const proto::SpellEntry& spell)
		{
			// When any spell starts casting, ensure we're properly marked as casting
			// This serves as confirmation that casting actually started
			if (spell.casttime() > 0)
			{
				m_isCasting = true;
				m_lastSpellCastTime = GetAsyncTimeMs();
				
				// Update timeout with the actual spell's cast time
				m_castingTimeoutEnd = m_lastSpellCastTime + spell.casttime() + 1000; // 1 second buffer
				
				// Ensure movement is stopped (safety check)
				auto& controlled = GetControlled();
				controlled.GetMover().StopMovement();
				m_movementState.Reset();
			}
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
				
				// Don't interfere with movement while casting
				if (m_isCasting)
				{
					return;
				}
				
				if (auto* victim = controlled.GetVictim())
				{
					// Immediate range check when we start moving - if we're already in range, stop
					const float attackRange = controlled.GetMeleeReach() + victim->GetMeleeReach();
					const float attackRangeSq = attackRange * attackRange;
					const float currentDistanceSq = victim->GetSquaredDistanceTo(controlled.GetMover().GetCurrentLocation(), false);
					
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
			// Notify script before removing threat
			if (m_script)
			{
				m_script->OnTargetDied(threatener);
			}

			RemoveThreat(threatener);
		});

		// Watch for unit despawned signal
		auto strongThreatener = std::static_pointer_cast<GameUnitS>(threatener.shared_from_this());
		m_miscSignals[guid] += threatener.despawned.connect([this, strongThreatener](GameObjectS&)
		{
			RemoveThreat(*strongThreatener);
		});
	}

	/**
	 * @brief Initializes the available spells list from the creature's entry data.
	 * 
	 * This method loads all spells defined in the creature's UnitEntry.creaturespells
	 * and creates CreatureSpell objects for each valid spell. It skips passive spells
	 * and determines appropriate ranges based on the spell data or creature entry.
	 * 
	 * The method prioritizes ranges from the creature entry, falls back to spell
	 * range data, and defaults to melee range if no range is specified.
	 */
	void CreatureAICombatState::InitializeSpells()
	{
		m_availableSpells.clear();
		
		const auto& controlled = GetControlled();
		const auto& entry = controlled.GetEntry();
		
		// Load spells from creature entry
		for (const auto& spellEntry : entry.creaturespells())
		{
			const auto* spell = controlled.GetProject().spells.getById(spellEntry.spellid());
			if (!spell)
			{
				continue;
			}
			
			// Skip passive spells
			if (spell->attributes(0) & spell_attributes::Passive)
			{
				continue;
			}
			
			// Determine range from spell or use defaults from creature spell entry
			float minRange = spellEntry.minrange();
			float maxRange = spellEntry.maxrange();
			
			// If no range specified in creature entry, use spell's range
			if (maxRange <= 0.0f && spell->has_rangetype())
			{
				if (const auto* rangeType = controlled.GetProject().ranges.getById(spell->rangetype()))
				{
					maxRange = rangeType->range();
				}
			}
			
			// Default to melee range if still no range
			if (maxRange <= 0.0f)
			{
				maxRange = controlled.GetMeleeReach() + 2.0f;
			}
			
			m_availableSpells.emplace_back(spell, minRange, maxRange, spellEntry.priority());
		}
	}

	/**
	 * @brief Determines the optimal combat behavior for this creature based on its spell list.
	 * 
	 * This method analyzes the creature's available spells and power type to determine
	 * whether it should behave as a melee fighter, caster, or ranged combatant:
	 * 
	 * - Rage users are always considered melee
	 * - Creatures with more ranged spells than melee spells become casters
	 * - All others default to melee behavior
	 * 
	 * @return The determined combat behavior type for this creature.
	 */
	CreatureAICombatState::CombatBehavior CreatureAICombatState::DetermineCombatBehavior() const
	{
		const auto& controlled = GetControlled();
		
		// Check if creature has mana/energy for spellcasting
		if (controlled.GetPowerType() == power_type::Rage)
		{
			return CombatBehavior::Melee; // Rage users are typically melee
		}
		
		// Count ranged spells vs melee spells
		uint32 rangedSpells = 0;
		uint32 meleeSpells = 0;
		
		for (const auto& creatureSpell : m_availableSpells)
		{
			if (creatureSpell.maxRange > controlled.GetMeleeReach() + 5.0f)
			{
				rangedSpells++;
			}
			else
			{
				meleeSpells++;
			}
		}
		
		// If creature has more ranged spells than melee spells, it's a caster
		if (rangedSpells > meleeSpells && rangedSpells > 0)
		{
			return CombatBehavior::Caster;
		}
		
		// Default to melee behavior
		return CombatBehavior::Melee;
	}

	/**
	 * @brief Checks whether the creature can currently cast spells.
	 * 
	 * This method verifies multiple conditions required for spell casting:
	 * - The creature is not already casting a spell
	 * - The creature has available spells in its spell list
	 * - The creature has sufficient power to cast at least one spell
	 * 
	 * @return True if the creature can cast spells, false otherwise.
	 */
	bool CreatureAICombatState::CanCastSpells() const
	{
		const auto& controlled = GetControlled();
		
		// Can't cast while casting
		if (m_isCasting)
		{
			return false;
		}
		
		// Need available spells
		if (m_availableSpells.empty())
		{
			return false;
		}
		
		// Need sufficient power for at least one spell
		const uint32 currentPower = controlled.GetPower();
		for (const auto& creatureSpell : m_availableSpells)
		{
			if (creatureSpell.canCast && currentPower >= creatureSpell.spell->cost())
			{
				return true;
			}
		}
		
		return false;
	}

	/**
	 * @brief Selects the most appropriate spell to cast against a target.
	 * 
	 * This method evaluates all available spells and selects the best one based on:
	 * - Spell availability (not on cooldown and can be cast)
	 * - Power cost (creature has sufficient mana/energy)
	 * - Range requirements (target is within spell's min/max range)
	 * - Priority (higher priority spells are preferred)
	 * 
	 * @param target The target to evaluate spells against.
	 * @return Pointer to the best spell to cast, or nullptr if no suitable spell found.
	 */
	const CreatureAICombatState::CreatureSpell* CreatureAICombatState::SelectBestSpell(const GameUnitS& target) const
	{
		const auto& controlled = GetControlled();
		const auto currentTime = GetAsyncTimeMs();
		const auto currentPower = controlled.GetPower();
		const auto distanceToTarget = controlled.GetSquaredDistanceTo(target.GetPosition(), true);
		
		const CreatureSpell* bestSpell = nullptr;
		uint32 highestPriority = 0;
		
		for (const auto& creatureSpell : m_availableSpells)
		{
			// Check if spell is available
			if (!creatureSpell.canCast || currentTime < creatureSpell.cooldownEnd)
			{
				continue;
			}
			
			// Check power cost
			if (currentPower < creatureSpell.spell->cost())
			{
				continue;
			}
			
			// Check range
			const float minRangeSq = creatureSpell.minRange * creatureSpell.minRange;
			const float maxRangeSq = creatureSpell.maxRange * creatureSpell.maxRange;
			
			if (distanceToTarget < minRangeSq || distanceToTarget > maxRangeSq)
			{
				continue;
			}
			
			// Check priority
			if (creatureSpell.priority > highestPriority)
			{
				bestSpell = &creatureSpell;
				highestPriority = creatureSpell.priority;
			}
		}
		
		return bestSpell;
	}

	/**
	 * @brief Attempts to cast a specified spell at a target.
	 * 
	 * This method handles the complete spell casting process:
	 * 1. Sets up the spell target map for unit targeting
	 * 2. Calls the controlled unit's CastSpell method
	 * 3. Updates internal casting state and cooldowns on success
	 * 4. Manages auto-attack interruption for spells with cast times
	 * 
	 * The method automatically tracks casting state and cooldowns, and handles
	 * the transition between spell casting and other combat actions.
	 * 
	 * @param spell The creature spell to cast.
	 * @param target The target unit for the spell.
	 * @return True if spell casting was initiated successfully, false otherwise.
	 */
	bool CreatureAICombatState::CastSpell(const CreatureSpell& spell, GameUnitS& target)
	{
		auto& controlled = GetControlled();

		// Cache spell data before casting (in case of reentrant calls)
		const auto* spellEntry = spell.spell;
		const uint32_t castTime = spellEntry->casttime();
		const uint32_t cooldown = spellEntry->cooldown();

		// Prevent reentrant ChooseNextAction during cast
		if (castTime > 0)
		{
			m_isCasting = true;
		}

		SpellTargetMap targetMap;
		targetMap.SetTargetMap(spell_cast_target_flags::Unit);
		targetMap.SetUnitTarget(target.GetGuid());

		const auto castResult = controlled.CastSpell(targetMap, *spellEntry, castTime);

		if (castResult == spell_cast_result::CastOkay)
		{
			const auto currentTime = GetAsyncTimeMs();

			for (auto& creatureSpell : m_availableSpells)
			{
				if (creatureSpell.spell == spellEntry)
				{
					creatureSpell.lastCastTime = currentTime;
					creatureSpell.cooldownEnd = currentTime + cooldown;
					break;
				}
			}

			if (castTime > 0)
			{
				m_lastSpellCastTime = currentTime;
				m_castingTimeoutEnd = currentTime + castTime + 1000;
				controlled.GetMover().StopMovement();
				m_movementState.Reset();
				controlled.StopAttack();
			}

			return true;
		}

		// Cast failed - reset casting flag if we set it
		if (castTime > 0)
		{
			m_isCasting = false;
		}

		return false;
	}

	/**
	 * @brief Updates the availability status of all creature spells based on cooldowns.
	 * 
	 * This method is called regularly to refresh which spells are available for casting.
	 * It updates the canCast flag for each spell based on whether enough time has passed
	 * since the spell was last used (cooldown period).
	 * 
	 * Should be called before any spell selection or casting attempts to ensure
	 * accurate availability information.
	 */
	void CreatureAICombatState::UpdateSpellCooldowns()
	{
		const auto currentTime = GetAsyncTimeMs();
		
		for (auto& creatureSpell : m_availableSpells)
		{
			// Update availability based on cooldown
			creatureSpell.canCast = (currentTime >= creatureSpell.cooldownEnd);
		}
	}

	/**
	 * @brief Checks if the creature is currently in optimal range for its combat behavior.
	 * 
	 * This method evaluates positioning based on the creature's combat behavior:
	 * - **Melee**: Within combined melee reach of creature and target
	 * - **Caster/Ranged**: Within optimal casting range (between min and max distance)
	 * 
	 * @param target The target to check range for.
	 * @return True if the creature is positioned optimally for its behavior type.
	 */
	bool CreatureAICombatState::IsInOptimalRange(const GameUnitS& target) const
	{
		const auto& controlled = GetControlled();
		const float distanceSq = controlled.GetSquaredDistanceTo(target.GetPosition(), true);
		
		switch (m_combatBehavior)
		{
		case CombatBehavior::Melee:
			{
				const float meleeRangeSq = (controlled.GetMeleeReach() + target.GetMeleeReach()) * 
										   (controlled.GetMeleeReach() + target.GetMeleeReach());
				const bool inRange = distanceSq <= meleeRangeSq;
				if (inRange)
				{
					const float distance = std::sqrt(distanceSq);
					const float acceptanceRadius = std::sqrt(meleeRangeSq);
					DLOG("Creature " << controlled.GetGuid() << " stopped at engagement range: distance=" 
						<< distance << " <= acceptanceRadius=" << acceptanceRadius);
				}
				return inRange;
			}
		case CombatBehavior::Caster:
		case CombatBehavior::Ranged:
			{
				const float minRangeSq = CASTER_MIN_RANGE * CASTER_MIN_RANGE;
				const float maxRangeSq = CASTER_OPTIMAL_RANGE * CASTER_OPTIMAL_RANGE;
				const bool inRange = distanceSq >= minRangeSq && distanceSq <= maxRangeSq;
				if (inRange)
				{
					const float distance = std::sqrt(distanceSq);
					const float acceptanceRadius = CASTER_OPTIMAL_RANGE;
					DLOG("Creature " << controlled.GetGuid() << " stopped at engagement range: distance=" 
						<< distance << " <= acceptanceRadius=" << acceptanceRadius);
				}
				return inRange;
			}
		}
		
		return false;
	}

	/**
	 * @brief Moves the creature to optimal combat range based on its behavior type.
	 * 
	 * This method implements intelligent positioning for different combat behaviors:
	 * 
	 * **Melee Behavior:**
	 * - Uses existing chase target logic to get within melee range
	 * 
	 * **Caster/Ranged Behavior:**
	 * - Maintains distance between CASTER_MIN_RANGE and CASTER_OPTIMAL_RANGE
	 * - Retreats if too close to target (< min range)
	 * - Advances if too far from target (> optimal range)
	 * - Uses target position prediction for moving targets
	 * - Updates movement state and handles stuck detection
	 * 
	 * @param target The target to position relative to.
	 * @return True if movement was initiated successfully or no movement needed.
	 */
	bool CreatureAICombatState::MoveToOptimalRange(GameUnitS& target)
	{
		auto& controlled = GetControlled();
		auto& mover = controlled.GetMover();
		
		// Nothing to do when rooted, under forced movement (fear/disorient), or casting
		if (controlled.IsRooted() || controlled.IsUnderForcedMovement() || m_isCasting)
		{
			return false;
		}
		
		const float currentDistanceSq = controlled.GetSquaredDistanceTo(target.GetPosition(), true);
		
		// Get threat targets for separation logic
		const auto threatTargets = GetThreatTargets();
		auto& separationManager = CreatureSeparationManager::Get();
		
		switch (m_combatBehavior)
		{
		case CombatBehavior::Melee:
			{
				// Use existing melee chase logic
				return ChaseTarget(target);
			}
		case CombatBehavior::Caster:
		case CombatBehavior::Ranged:
			{
				const float minRangeSq = CASTER_MIN_RANGE * CASTER_MIN_RANGE;
				const float optimalRangeSq = CASTER_OPTIMAL_RANGE * CASTER_OPTIMAL_RANGE;
				
				// If too close, move away
				if (currentDistanceSq < minRangeSq)
				{
					// Calculate position away from target
					const Vector3 targetPos = target.GetPosition();
					const Vector3 ourPos = controlled.GetPosition();
					const Vector3 direction = (ourPos - targetPos).NormalizedCopy();
					Vector3 retreatPos = targetPos + direction * CASTER_OPTIMAL_RANGE;
					
					// Apply separation logic to avoid stacking with nearby creatures
					retreatPos = separationManager.AdjustTargetForSeparation(controlled, retreatPos, threatTargets);
					
					// Use combat range factor for engagement range enforcement
					// Stop at roughly CASTER_MIN_RANGE to ensure safe distance
					const float retreatEngagementRange = CASTER_MIN_RANGE * COMBAT_RANGE_FACTOR;
					if (mover.MoveTo(retreatPos, retreatEngagementRange))
					{
						m_movementState.UpdateTarget(retreatPos, CASTER_OPTIMAL_RANGE);
						m_stuckCounter = 0;
						return true;
					}
				}
				// If too far, move closer
				else if (currentDistanceSq > optimalRangeSq)
				{
					Vector3 targetPosition = PredictTargetPosition(target);
					
					// Apply separation logic to avoid stacking with nearby creatures
					targetPosition = separationManager.AdjustTargetForSeparation(controlled, targetPosition, threatTargets);
					
					// Use combat range factor for engagement range enforcement
					// Stop at roughly CASTER_OPTIMAL_RANGE to ensure proper spell casting distance
					const float approachEngagementRange = CASTER_OPTIMAL_RANGE * COMBAT_RANGE_FACTOR;
					if (mover.MoveTo(targetPosition, approachEngagementRange))
					{
						m_movementState.UpdateTarget(targetPosition, CASTER_OPTIMAL_RANGE);
						m_stuckCounter = 0;
						return true;
					}
				}
				else
				{
					// We're in optimal range, no movement needed
					return true;
				}
			}
		}
		
		return !HandleMovementFailure();
	}
	
	Vector3 CreatureAICombatState::CalculateFormationPosition(const GameUnitS& target, const Vector3& approachPosition, float standoffDistance) const
	{
		const auto& controlled = GetControlled();
		
		// Count how many other creatures are attacking the same target and determine our slot
		int totalAttackers = 0;
		int ourSlot = 0;
		const uint64 ourGuid = controlled.GetGuid();

		target.ForEachAttacker([&](const GameUnitS& attacker)
		{
			if (&attacker == &target)
			{
				return;
			}

			if (attacker.GetGuid() == ourGuid)
			{
				ourSlot = totalAttackers;
			}

			totalAttackers++;
		});

		// With 0 or 1 attackers, no formation needed - go straight at the target
		if (totalAttackers <= 1)
		{
			return approachPosition;
		}

		// Calculate the base angle from target to the approach position
		const Vector3 targetPos = target.GetPosition();
		const Vector3 toApproach = approachPosition - targetPos;
		const float baseAngle = std::atan2(toApproach.z, toApproach.x);

		// Spread creatures in a semicircle centered on the base approach angle.
		// The spread increases with more attackers but is capped.
		const float totalSpread = std::min(
			static_cast<float>(totalAttackers - 1) * FORMATION_ANGLE_STEP,
			FORMATION_MAX_ANGLE);
		const float startAngle = baseAngle - totalSpread * 0.5f;
		const float angleStep = (totalAttackers > 1)
			? totalSpread / static_cast<float>(totalAttackers - 1)
			: 0.0f;

		const float slotAngle = startAngle + static_cast<float>(ourSlot) * angleStep;

		// The destination must be the target's position itself — MoveTo(dest, acceptanceRadius)
		// shortens the path by acceptanceRadius metres before dest. If we pre-offset dest by
		// standoffDistance away from the target, MoveTo then stops an additional acceptanceRadius
		// short of that, leaving the creature ~2× the intended range away.
		//
		// To steer the creature onto the correct arc slot we nudge the destination by a tiny
		// amount in the slot direction. MoveTo will end up stopping acceptanceRadius before the
		// target on that approach vector, placing the creature on the right side.
		Vector3 formationPos;
		formationPos.x = targetPos.x + std::cos(slotAngle) * 0.5f;
		formationPos.y = approachPosition.y;
		formationPos.z = targetPos.z + std::sin(slotAngle) * 0.5f;

		return formationPos;
	}

	// === Script Support Methods ===

	std::vector<GameUnitS*> CreatureAICombatState::GetThreatTargets() const
	{
		std::vector<GameUnitS*> result;
		result.reserve(m_threat.size());

		for (const auto& pair : m_threat)
		{
			if (auto threatener = pair.second.threatener.lock())
			{
				if (threatener->IsAlive())
				{
					result.push_back(threatener.get());
				}
			}
		}

		return result;
	}

	void CreatureAICombatState::AddThreatFromScript(GameUnitS& threatener, float amount)
	{
		const uint64 guid = threatener.GetGuid();
		const auto it = m_threat.find(guid);
		if (it != m_threat.end())
		{
			it->second.amount = std::max(0.0f, it->second.amount + amount);
		}
		else if (amount >= 0.0f)
		{
			AddThreat(threatener, amount);
		}
	}

	void CreatureAICombatState::ResetAllThreatFromScript()
	{
		for (auto& pair : m_threat)
		{
			pair.second.amount = 0.0f;
		}
	}
}
