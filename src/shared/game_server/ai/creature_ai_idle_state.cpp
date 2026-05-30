// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_idle_state.h"
#include "game_server/ai/creature_ai.h"
#include "objects/game_creature_s.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"
#include <limits>

namespace mmo
{
	CreatureAIIdleState::CreatureAIIdleState(CreatureAI& ai)
		: CreatureAIState(ai)
		, m_waitCountdown(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIIdleState::~CreatureAIIdleState() = default;

	void CreatureAIIdleState::OnEnter()
	{
		CreatureAIState::OnEnter();
		GetControlled().SetMovementMode(unit_movement_mode::Walk);

		m_connections += m_waitCountdown.ended.connect(*this, &CreatureAIIdleState::OnWaitCountdownExpired);
		m_connections += GetAI().GetControlled().GetMover().targetReached.connect(*this, &CreatureAIIdleState::OnTargetReached);
		m_connections += GetAI().GetControlled().threatened.connect([ai = &GetAI()]<typename T0, typename T1>(T0&& instigator, T1&& threat)
		{
			ai->OnThreatened(std::forward<T0>(instigator), std::forward<T1>(threat));
		});

		const auto& location = GetControlled().GetPosition();

		m_unitWatcher = GetControlled().GetWorldInstance()->GetUnitFinder().WatchUnits(Circle(location.x, location.z, 40.0f), [this](GameUnitS& unit, bool isVisible) -> bool
			{
				const auto& controlled = GetControlled();
				if (&unit == &controlled)
				{
					return true;
				}

				if (!controlled.IsAlive())
				{
					return true;
				}

				if (!unit.IsAlive())
				{
					return true;
				}
			
				// Should we even be allowed to see this unit based on its current visibility state?
				if (!unit.CanBeSeenBy(controlled))
				{
					return true;
				}

				const float dist = sqrtf(controlled.GetSquaredDistanceTo(unit.GetPosition(), true));

				// TODO: Check if we are hostile against target unit. For now we will only attack player characters
				if (controlled.UnitIsEnemy(unit))
				{
					const int32 ourLevel = controlled.Get<int32>(object_fields::Level);
					const int32 otherLevel = unit.Get<int32>(object_fields::Level);
					const int32 diff = ::abs(ourLevel - otherLevel);

					// Check distance
					float reqDist = 20.0f;
					if (ourLevel < otherLevel)
					{
						reqDist = Clamp<float>(reqDist - diff * 2, 1.0f, 40.0f);
					}
					else if (otherLevel < ourLevel)
					{
						reqDist = Clamp<float>(reqDist + diff, 1.0f, 40.0f);
					}

					if (dist > reqDist)
					{
						return true;
					}

					// Skip engagement when the creature cannot see the potential target.
					if (MapData* mapData = GetAI().GetControlled().GetWorldInstance()->GetMapData())
					{
						if (!mapData->IsInLineOfSight(controlled.GetPosition(), unit.GetPosition()))
						{
							return true;
						}
					}

					auto strongUnit = unit.shared_from_this();
					auto strongThis = shared_from_this();
					GetAI().GetControlled().GetWorldInstance()->GetUniverse().Post([strongUnit, strongThis]()
						{
							strongThis->GetAI().EnterCombat(strongUnit->AsUnit());
						});
				}
				else if (controlled.UnitIsFriendly(unit))
				{
					// It's our ally - check if we should assist in combat
					if (isVisible)
					{
						if (dist <= 8.0f && unit.IsInCombat())
						{
							// Prefer the ally's current victim. When the victim field is
							// cleared (e.g. because the ally is feared / stunned / sleeping)
							// fall back to the ally's combat participant list. Combat participants
							// are the players/units that have generated threat on this creature
							// (populated in AddThreat regardless of CC state), so they remain
							// valid even when SetTarget(0) has cleared the visual victim field.
							GameUnitS* victim = unit.GetVictim();

							if (victim == nullptr)
							{
								if (auto* allyCreature = dynamic_cast<GameCreatureS*>(&unit))
								{
									allyCreature->ForEachCombatParticipant([&](GameUnitS& participant)
									{
										if (victim == nullptr && controlled.UnitIsEnemy(participant))
										{
											victim = &participant;
										}
									});
								}
							}

							if (victim != nullptr)
							{
								// Is the enemy of our friend our enemy?
								if (!controlled.UnitIsEnemy(*victim))
								{
									return false;
								}

								// Only assist if we can actually see the ally being attacked.
								if (MapData* mapData = GetAI().GetControlled().GetWorldInstance()->GetMapData())
								{
									if (!mapData->IsInLineOfSight(controlled.GetPosition(), unit.GetPosition()))
									{
										return true;
									}
								}

								auto strongVictim = victim->shared_from_this();
								auto strongThis = shared_from_this();
								GetAI().GetControlled().GetWorldInstance()->GetUniverse().Post([strongVictim, strongThis]()
									{
										strongThis->GetAI().EnterCombat(strongVictim->AsUnit());
									});
								return true;
							}
						}
					}
				}

				return true;
			});
		ASSERT(m_unitWatcher);

		OnCreatureMovementChanged();

		m_unitWatcher->Start();
	}

	void CreatureAIIdleState::OnLeave()
	{
		ASSERT(m_unitWatcher);
		m_unitWatcher.reset();

		m_waitCountdown.Cancel();
		m_connections.disconnect();

		// Save the world position so the reset state can return here instead of spawn.
		GetAI().SavePatrolReturnPosition(GetControlled().GetMover().GetCurrentLocation());

		// Determine which waypoint the creature should resume from.
		// m_chainProgressIndex tracks which waypoint in the chain the creature is heading
		// toward (or has just reached). Save that patrol index as the resume point.
		if (!m_currentChainWaypointIndices.empty())
		{
			if (m_chainProgressIndex < m_currentChainWaypointIndices.size())
			{
				GetAI().SavePatrolWaypointIndex(m_currentChainWaypointIndices[m_chainProgressIndex]);
			}
			else
			{
				// All waypoints in the chain were reached; save the post-chain index.
				GetAI().SavePatrolWaypointIndex(m_nextPatrolWaypointIndex);
			}
		}
		else
		{
			GetAI().SavePatrolWaypointIndex(m_nextPatrolWaypointIndex);
		}

		CreatureAIState::OnLeave();
	}

	void CreatureAIIdleState::OnCreatureMovementChanged()
	{
		m_waitCountdown.Cancel();

		if (GetControlled().GetMovementType() == creature_movement::None)
		{
			GetControlled().GetMover().StopMovement();
			return;
		}

		if (GetControlled().GetMovementType() == creature_movement::Patrol)
		{
			if (GetControlled().HasPatrolWaypoints())
			{
				if (GetAI().HasSavedPatrolWaypointIndex())
				{
					// Restore the chain start recorded when we left idle so the creature
					// resumes in the same direction it was travelling.
					m_nextPatrolWaypointIndex = GetAI().GetSavedPatrolWaypointIndex();
					GetAI().ClearSavedPatrolWaypointIndex();
				}
				else
				{
					m_nextPatrolWaypointIndex = FindClosestPatrolWaypointIndex();
				}
				AdvanceIdleMovement();
			}
			else
			{
				GetControlled().GetMover().StopMovement();
			}

			return;
		}

		if (GetControlled().GetMovementType() == creature_movement::Random)
		{
			OnTargetReached();
		}
	}

	void CreatureAIIdleState::OnControlledMoved()
	{
		if (m_unitWatcher)
		{
			const auto& loc = GetControlled().GetPosition();
			m_unitWatcher->SetShape(Circle(loc.x, loc.z, 40.0f));
		}

		// Track progress through the current waypoint chain so we know where to resume.
		if (!m_currentChainWaypointIndices.empty() &&
			m_chainProgressIndex < m_currentChainWaypointIndices.size())
		{
			const auto& patrolWaypoints = GetControlled().GetPatrolWaypoints();
			const size_t wpIdx = m_currentChainWaypointIndices[m_chainProgressIndex];
			const float distSq = GetControlled().GetSquaredDistanceTo(patrolWaypoints[wpIdx].position, true);

			// If the creature is close to the current target waypoint, it has reached it.
			// Advance progress to the next waypoint in the chain.
			if (distSq <= 4.0f)
			{
				++m_chainProgressIndex;
			}
		}
	}

	void CreatureAIIdleState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		GetAI().EnterCombat(attacker);
	}

	void CreatureAIIdleState::OnWaitCountdownExpired()
	{
		AdvanceIdleMovement();
	}

	void CreatureAIIdleState::OnTargetReached()
	{
		if (GetControlled().GetMovementType() == creature_movement::Patrol)
		{
			const auto& patrolWaypoints = GetControlled().GetPatrolWaypoints();
			if (patrolWaypoints.empty())
			{
				return;
			}

			const size_t waypointCount = patrolWaypoints.size();
			const size_t reachedWaypointIndex = (m_nextPatrolWaypointIndex + waypointCount - 1) % waypointCount;
			StartWait(patrolWaypoints[reachedWaypointIndex].waitTime);
			return;
		}

		StartWait(2000);
	}

	void CreatureAIIdleState::AdvanceIdleMovement()
	{
		// Don't issue idle wander/patrol moves while fear or disorient controller is driving movement
		if (GetControlled().IsUnderForcedMovement())
		{
			return;
		}

		if (GetControlled().GetMovementType() == creature_movement::Patrol)
		{
			MoveToNextPatrolWaypoint();
			return;
		}

		if (GetControlled().GetMovementType() == creature_movement::Random)
		{
			MoveToRandomPointInRange();
		}
	}

	void CreatureAIIdleState::MoveToNextPatrolWaypoint()
	{
		const auto& patrolWaypoints = GetControlled().GetPatrolWaypoints();
		if (patrolWaypoints.empty())
		{
			return;
		}

		const size_t waypointCount = patrolWaypoints.size();

		// Collect consecutive waypoints into a single movement chain.
		// We keep adding waypoints as long as the one we are leaving has no wait time.
		// The chain always ends at (and includes) the first waypoint that has a wait time,
		// or after a full loop if all waypoints have waitTime == 0.
		std::vector<Vector3> chain;
		size_t chainIndex = m_nextPatrolWaypointIndex % waypointCount;
		// Record all waypoint indices in this chain for progress tracking.
		m_currentChainWaypointIndices.clear();
		m_chainProgressIndex = 0;
		size_t stepsCollected = 0;

		while (stepsCollected < waypointCount)
		{
			chain.push_back(patrolWaypoints[chainIndex].position);
			m_currentChainWaypointIndices.push_back(chainIndex);
			++stepsCollected;

			// Stop collecting after the first waypoint that has a non-zero wait,
			// because the creature should pause there.
			if (patrolWaypoints[chainIndex].waitTime > 0)
			{
				chainIndex = (chainIndex + 1) % waypointCount;
				break;
			}

			chainIndex = (chainIndex + 1) % waypointCount;
		}

		// Advance the index to point to the waypoint AFTER the last one in the chain.
		m_nextPatrolWaypointIndex = chainIndex;

		// If the creature is already standing on the first waypoint of the chain, skip it.
		if (!chain.empty() && GetControlled().GetSquaredDistanceTo(chain.front(), true) <= 0.25f)
		{
			chain.erase(chain.begin());
		}

		if (chain.empty())
		{
			// Already at destination — treat as immediately arrived.
			const size_t reachedIndex = (m_nextPatrolWaypointIndex + waypointCount - 1) % waypointCount;
			StartWait(patrolWaypoints[reachedIndex].waitTime);
			return;
		}

		if (!GetControlled().GetMover().MoveAlongWaypoints(chain, 0.0f))
		{
			StartWait(250);
		}
	}

	size_t CreatureAIIdleState::FindClosestPatrolWaypointIndex() const
	{
		const auto& patrolWaypoints = GetControlled().GetPatrolWaypoints();
		if (patrolWaypoints.empty())
		{
			return 0;
		}

		size_t closestWaypointIndex = 0;
		float closestDistanceSq = std::numeric_limits<float>::max();

		for (size_t waypointIndex = 0; waypointIndex < patrolWaypoints.size(); ++waypointIndex)
		{
			const float distanceSq = GetControlled().GetSquaredDistanceTo(patrolWaypoints[waypointIndex].position, true);
			if (distanceSq < closestDistanceSq)
			{
				closestDistanceSq = distanceSq;
				closestWaypointIndex = waypointIndex;
			}
		}

		return closestWaypointIndex;
	}

	void CreatureAIIdleState::StartWait(const uint32 waitTimeMs)
	{
		m_waitCountdown.SetEnd(GetAsyncTimeMs() + waitTimeMs);
	}

	void CreatureAIIdleState::MoveToRandomPointInRange()
	{
		// Make random movement
		UnitMover& mover = GetAI().GetControlled().GetMover();

		const auto* map = GetAI().GetControlled().GetWorldInstance()->GetMapData();
		ASSERT(map);

		if (Vector3 position; map->FindRandomPointAroundCircle(GetAI().GetHome().position, 7.5f, position))
		{
			mover.MoveTo(position, 0.0f);
		}
		else
		{
			OnTargetReached();
		}
	}
}
