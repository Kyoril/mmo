// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_api.h"

#include "console/console_var.h"
#include "game_states/world_state.h"
#include "player_controller.h"

#include "base/clock.h"
#include "base/macros.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
#include "math/constants.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mmo
{
	extern uint32 g_mapId;

	namespace
	{
		constexpr const char* EnabledCVarName = "botApiEnabled";
		constexpr const char* NavPathCVarName = "botNavPath";

		/// MoveToTarget re-paths at most this often (milliseconds)...
		constexpr GameTime RepathIntervalMs = 500;

		/// ...and only once the target has moved this far (yards) from the path's end.
		constexpr float RepathDistance = 2.0f;

		/// A path ending this close (yards, XZ) to the point MoveTo asked for reached it. Nav
		/// clamps goals onto the mesh, so the path's end rarely equals the requested point.
		constexpr float GoalReachedTolerance = 2.0f;

		/// Facing differences below this (radians) are not worth turning for.
		constexpr float FacingEpsilon = 0.03f;

		/// Facing packets are sent at most this often (milliseconds), like mouse steering does.
		constexpr GameTime FacingSendIntervalMs = 100;

		float PlanarDistance(const Vector3& a, const Vector3& b)
		{
			const float dx = a.x - b.x;
			const float dz = a.z - b.z;
			return std::sqrt(dx * dx + dz * dz);
		}

		/// Smallest absolute angle between two facings, in [0, pi].
		float AngleBetween(const float a, const float b)
		{
			float delta = std::fmod(std::abs(a - b), TwoPi);
			if (delta > Pi)
			{
				delta = TwoPi - delta;
			}

			return delta;
		}
	}

	namespace bot_api
	{
		void RegisterConsoleVars()
		{
			ConsoleVarMgr::RegisterConsoleVar(EnabledCVarName, "Enables the dev-only Lua bot API. 0 = bot functions do nothing.", "0");
			ConsoleVarMgr::RegisterConsoleVar(NavPathCVarName, "Nav data folder for the bot API, absolute or relative to dataPath. Dev builds only.", "../editor/nav");
		}

		bool IsEnabled()
		{
			const ConsoleVar* enabled = ConsoleVarMgr::FindConsoleVar(EnabledCVarName);
			return enabled && enabled->GetBoolValue();
		}
	}

	BotApi::BotApi(RealmConnector& realmConnector, const proto_client::Project& project)
		: m_realmConnector(realmConnector)
		, m_project(project)
		, m_nav(project.maps)
	{
	}

	void BotApi::Update(const GameTime now)
	{
		if (m_mode == MoveMode::None)
		{
			return;
		}

		PlayerController* controller = WorldState::GetPlayerController();
		GameUnitC* unit = controller ? controller->GetControlledUnit().get() : nullptr;
		if (!bot_api::IsEnabled() || !unit || !unit->IsAlive())
		{
			Stop(MoveStatus::Idle);
			return;
		}

		// The player controller ignores input during these, and so does the path: it resumes
		// from wherever the unit ends up.
		if (unit->IsBeingMoved() || unit->IsMovementControlledByServer())
		{
			ReleaseMoveKey();
			return;
		}

		if (m_mode == MoveMode::Target)
		{
			const std::shared_ptr<GameUnitC> target = ObjectMgr::GetSelectedObject();
			if (!target)
			{
				Stop(MoveStatus::NoPath);
				return;
			}

			if (unit->GetPosition().GetDistanceTo(target->GetPosition()) <= m_targetRange)
			{
				Stop(MoveStatus::Arrived);
				return;
			}

			if (now >= m_nextRepath)
			{
				m_nextRepath = now + RepathIntervalMs;
				if (PlanarDistance(target->GetPosition(), m_follower.GetGoal()) > RepathDistance &&
					!StartPath(*unit, target->GetPosition(), now))
				{
					return;
				}
			}
		}

		FollowPath(*controller, *unit, now, false);
	}

	void BotApi::Reset()
	{
		Stop(MoveStatus::Idle);
	}

	bool BotApi::MoveTo(const Vector3& goal)
	{
		PlayerController* controller = WorldState::GetPlayerController();
		GameUnitC* unit = controller ? controller->GetControlledUnit().get() : nullptr;
		if (!unit)
		{
			return false;
		}

		m_mode = MoveMode::Point;
		m_pointGoal = goal;

		const GameTime now = GetAsyncTimeMs();
		if (!StartPath(*unit, goal, now))
		{
			return false;
		}

		FollowPath(*controller, *unit, now, true);
		return m_status == MoveStatus::Moving || m_status == MoveStatus::Arrived;
	}

	bool BotApi::MoveToTarget(const float range)
	{
		PlayerController* controller = WorldState::GetPlayerController();
		GameUnitC* unit = controller ? controller->GetControlledUnit().get() : nullptr;
		const std::shared_ptr<GameUnitC> target = ObjectMgr::GetSelectedObject();
		if (!unit || !target)
		{
			return false;
		}

		m_mode = MoveMode::Target;
		m_targetRange = std::max(0.0f, range);

		if (unit->GetPosition().GetDistanceTo(target->GetPosition()) <= m_targetRange)
		{
			Stop(MoveStatus::Arrived);
			return true;
		}

		const GameTime now = GetAsyncTimeMs();
		m_nextRepath = now + RepathIntervalMs;
		if (!StartPath(*unit, target->GetPosition(), now))
		{
			return false;
		}

		FollowPath(*controller, *unit, now, true);
		return m_status == MoveStatus::Moving || m_status == MoveStatus::Arrived;
	}

	bool BotApi::StartPath(GameUnitC& unit, const Vector3& goal, const GameTime now)
	{
		std::vector<Vector3> points;
		switch (m_nav.FindPath(g_mapId, unit.GetPosition(), goal, points))
		{
		case BotNavResult::Ok:
			break;
		case BotNavResult::NoNavmesh:
			Stop(MoveStatus::NoNavmesh);
			return false;
		case BotNavResult::NoPath:
			Stop(MoveStatus::NoPath);
			return false;
		}

		m_follower.Start(std::move(points), now);
		m_status = MoveStatus::Moving;
		return true;
	}

	void BotApi::FollowPath(PlayerController& controller, GameUnitC& unit, const GameTime now, const bool sendFacingNow)
	{
		const BotPathStep step = m_follower.Advance(unit.GetPosition(), now);
		switch (step.kind)
		{
		case BotPathStep::Finished:
			// MoveToTarget arrives through its range check before the path runs out; a path that
			// ends first stopped short of the target.
			Stop(m_mode == MoveMode::Point && PlanarDistance(m_follower.GetGoal(), m_pointGoal) <= GoalReachedTolerance
				? MoveStatus::Arrived
				: MoveStatus::NoPath);
			return;
		case BotPathStep::Stuck:
			Stop(MoveStatus::Stuck);
			return;
		case BotPathStep::Steer:
			// The facing goes out before the move key, so the start-move packet already carries it.
			FacePoint(unit, step.steerTarget, now, sendFacingNow);
			controller.SetControlBit(ControlFlags::MoveForwardKey, true);
			m_drivingKey = true;
			return;
		}
	}

	void BotApi::FacePoint(GameUnitC& unit, const Vector3& point, const GameTime now, const bool sendNow)
	{
		const Radian facing = unit.GetAngle(point.x, point.z);
		const bool turn = AngleBetween(facing.GetValueRadians(), unit.GetFacing().GetValueRadians()) > FacingEpsilon;
		if (!turn && !m_facingSendPending)
		{
			return;
		}

		if (sendNow || now >= m_nextFacingSend)
		{
			unit.SetFacing(turn ? facing : unit.GetFacing());
			m_nextFacingSend = now + FacingSendIntervalMs;
			m_facingSendPending = false;
			return;
		}

		unit.SetFacingLocal(facing);
		m_facingSendPending = true;
	}

	void BotApi::Stop(const MoveStatus status)
	{
		ReleaseMoveKey();
		m_follower.Clear();
		m_mode = MoveMode::None;
		m_status = status;
	}

	void BotApi::ReleaseMoveKey()
	{
		if (!m_drivingKey)
		{
			return;
		}

		m_drivingKey = false;
		if (PlayerController* controller = WorldState::GetPlayerController())
		{
			controller->SetControlBit(ControlFlags::MoveForwardKey, false);
		}
	}

	const char* BotApi::GetMoveStatusName() const
	{
		switch (m_status)
		{
		case MoveStatus::Idle:
			return "idle";
		case MoveStatus::Moving:
			return "moving";
		case MoveStatus::Arrived:
			return "arrived";
		case MoveStatus::NoPath:
			return "no_path";
		case MoveStatus::NoNavmesh:
			return "no_navmesh";
		case MoveStatus::Stuck:
			return "stuck";
		}

		UNREACHABLE();
		return "idle";
	}
}
