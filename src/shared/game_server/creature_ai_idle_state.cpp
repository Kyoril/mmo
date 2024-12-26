
#include "creature_ai_idle_state.h"
#include "creature_ai.h"
#include "game_creature_s.h"
#include "world_instance.h"

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

		m_connections += m_waitCountdown.ended.connect(*this, &CreatureAIIdleState::OnWaitCountdownExpired);
		m_connections += GetAI().GetControlled().GetMover().targetReached.connect(*this, &CreatureAIIdleState::OnTargetReached);
		m_connections += GetAI().GetControlled().threatened.connect([ai = &GetAI()]<typename T0, typename T1>(T0&& instigator, T1&& threat)
		{
			ai->OnThreatened(std::forward<T0>(instigator), std::forward<T1>(threat));
		});


		const auto& location = GetControlled().GetPosition();

		m_unitWatcher = GetControlled().GetWorldInstance()->GetUnitFinder().WatchUnits(Circle(location.x, location.y, 40.0f), [this](GameUnitS& unit, bool isVisible) -> bool
			{
				const auto& controlled = GetControlled();
				if (&unit == &controlled)
				{
					return false;
				}

				if (!controlled.IsAlive())
				{
					return false;
				}

				if (!unit.IsAlive())
				{
					return false;
				}

				// TODO: Check if we are hostile against target unit. For now we will only attack player characters
				if (!controlled.UnitIsEnemy(unit))
				{
					return false;
				}

				const int32 ourLevel = controlled.Get<int32>(object_fields::Level);
				const int32 otherLevel = unit.Get<int32>(object_fields::Level);
				const int32 diff = ::abs(ourLevel - otherLevel);

				const float dist = sqrtf(controlled.GetSquaredDistanceTo(unit.GetPosition(), true));

				// Check distance
				float reqDist = 20.0f;
				if (ourLevel < otherLevel)
				{
					reqDist = Clamp<float>(reqDist - diff, 5.0f, 40.0f);
				}
				else if (otherLevel < ourLevel)
				{
					reqDist = Clamp<float>(reqDist + diff, 5.0f, 40.0f);
				}

				if (dist > reqDist)
				{
					return false;
				}

				// TODO: Line of sight check

				GetAI().EnterCombat(unit);
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

		m_connections.disconnect();

		CreatureAIState::OnLeave();
	}

	void CreatureAIIdleState::OnCreatureMovementChanged()
	{
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
			m_unitWatcher->SetShape(Circle(loc.x, loc.y, 40.0f));
		}
	}

	void CreatureAIIdleState::OnDamage(GameUnitS& attacker)
	{
		CreatureAIState::OnDamage(attacker);

		GetAI().EnterCombat(attacker);
	}

	void CreatureAIIdleState::OnWaitCountdownExpired()
	{
		MoveToRandomPointInRange();
	}

	void CreatureAIIdleState::OnTargetReached()
	{
		m_waitCountdown.SetEnd(GetAsyncTimeMs() + 2000);
	}

	void CreatureAIIdleState::MoveToRandomPointInRange()
	{
		// Make random movement
		UnitMover& mover = GetAI().GetControlled().GetMover();

		// Generate a random position around 0,0,0 in a x and z radius of 10
		const float x = (rand() % 20) - 10;
		const float z = (rand() % 20) - 10;

		const Vector3 position = GetAI().GetHome().position + Vector3(x, 0.0f, z);
		mover.MoveTo(position);
	}
}
