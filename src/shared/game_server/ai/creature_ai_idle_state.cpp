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

					// TODO: Line of sight check

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
						if (dist <= 8.0f)
						{
							auto* victim = unit.GetVictim();
							if (unit.IsInCombat() && victim != nullptr)
							{
								// Is the enemy of our friend our enemy?
								if (!controlled.UnitIsEnemy(*victim))
								{
									return false;
								}

								// TODO: Line of sight check

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

		if (GetControlled().GetMovementType() == creature_movement::Waypoints)
		{
			if (GetControlled().HasPatrolWaypoints())
			{
				m_nextPatrolWaypointIndex = FindClosestPatrolWaypointIndex();
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
		if (GetControlled().GetMovementType() == creature_movement::Waypoints)
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
		if (GetControlled().GetMovementType() == creature_movement::Waypoints)
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

		const size_t waypointIndex = m_nextPatrolWaypointIndex % patrolWaypoints.size();
		const auto& waypoint = patrolWaypoints[waypointIndex];
		m_nextPatrolWaypointIndex = (waypointIndex + 1) % patrolWaypoints.size();

		if (GetControlled().GetSquaredDistanceTo(waypoint.position, true) <= 0.25f)
		{
			StartWait(waypoint.waitTime);
			return;
		}

		const float walkSpeed = GetControlled().GetSpeed(movement_type::Walk);
		if (!GetControlled().GetMover().MoveTo(waypoint.position, 0.0f))
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
