// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "game_server/ai/creature_ai_alert_state.h"
#include "game_server/ai/creature_ai.h"
#include "objects/game_creature_s.h"
#include "game_server/world/universe.h"
#include "game_server/world/world_instance.h"

#include <cmath>

namespace mmo
{
	CreatureAIAlertState::CreatureAIAlertState(CreatureAI& ai, GameUnitS& target)
		: CreatureAIState(ai)
		, m_target(std::static_pointer_cast<GameUnitS>(target.shared_from_this()))
		, m_checkCountdown(ai.GetControlled().GetTimers())
	{
	}

	CreatureAIAlertState::~CreatureAIAlertState() = default;

	void CreatureAIAlertState::OnEnter()
	{
		CreatureAIState::OnEnter();

		auto& controlled = GetControlled();

		const auto target = m_target.lock();
		if (!target)
		{
			PostIdle();
			return;
		}

		// Stop and face the spot where the stealthed unit was noticed
		controlled.GetMover().StopMovement();
		controlled.SetFacing(controlled.GetAngle(*target));

		m_spotDistanceSq = controlled.GetSquaredDistanceTo(target->GetPosition(), true);
		m_alertEnd = GetAsyncTimeMs() + AlertDuration;

		// Let the spotted unit know it has been noticed so its client can play an alert sound
		if (auto* watcher = target->GetNetUnitWatcher())
		{
			watcher->OnStealthDetected(controlled.GetGuid());
		}

		m_connections += m_checkCountdown.ended.connect(*this, &CreatureAIAlertState::OnCheckTimer);
		m_checkCountdown.SetEnd(GetAsyncTimeMs() + AlertCheckInterval);
	}

	void CreatureAIAlertState::OnLeave()
	{
		m_checkCountdown.Cancel();
		m_connections.disconnect();

		CreatureAIState::OnLeave();
	}

	void CreatureAIAlertState::OnDamage(GameUnitS& attacker)
	{
		PostEnterCombat(std::static_pointer_cast<GameUnitS>(attacker.shared_from_this()));
	}

	void CreatureAIAlertState::OnCheckTimer()
	{
		auto& controlled = GetControlled();

		const auto target = m_target.lock();
		if (!target || !target->IsAlive() || !controlled.IsAlive())
		{
			PostIdle();
			return;
		}

		// Target dropped stealth right in front of us: engage immediately
		if (target->GetVisibility() != unit_visibility::GroupStealth && controlled.UnitIsEnemy(*target))
		{
			PostEnterCombat(target);
			return;
		}

		const bool visible = target->CanBeSeenBy(controlled);
		const float distanceSq = controlled.GetSquaredDistanceTo(target->GetPosition(), true);

		if (visible)
		{
			// Came closer while being watched: engage before the alert time runs out
			const float engageDistance = ::sqrtf(m_spotDistanceSq) - EngageDistanceDelta;
			if (engageDistance > 0.0f && distanceSq <= engageDistance * engageDistance)
			{
				PostEnterCombat(target);
				return;
			}

			// Keep facing the unit while it sneaks around in view
			controlled.SetFacing(controlled.GetAngle(*target));
		}

		if (GetAsyncTimeMs() >= m_alertEnd)
		{
			// Still visible after the full alert duration: engage. Otherwise the creature
			// loses interest and resumes its idle movement.
			if (visible)
			{
				PostEnterCombat(target);
			}
			else
			{
				PostIdle();
			}
			return;
		}

		m_checkCountdown.SetEnd(GetAsyncTimeMs() + AlertCheckInterval);
	}

	void CreatureAIAlertState::PostEnterCombat(const std::shared_ptr<GameUnitS>& target)
	{
		// State transitions are posted to the universe because this method is called from
		// signal handlers (countdown / damage) and SetState destroys this state instance.
		auto strongThis = shared_from_this();
		GetControlled().GetWorldInstance()->GetUniverse().Post([strongThis, target]()
			{
				strongThis->GetAI().EnterCombat(*target);
			});
	}

	void CreatureAIAlertState::PostIdle()
	{
		auto strongThis = shared_from_this();
		GetControlled().GetWorldInstance()->GetUniverse().Post([strongThis]()
			{
				strongThis->GetAI().Idle();
			});
	}
}
