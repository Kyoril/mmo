// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_nav.h"
#include "bot_path_follower.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

struct lua_State;

namespace mmo
{
	class GameUnitC;
	class PlayerController;
	class RealmConnector;

	namespace proto_client
	{
		class Project;
	}

	/// The dev-only bot API's runtime switch. Compiled in only with MMO_WITH_CLIENT_BOT_API.
	namespace bot_api
	{
		/// @brief Registers the CVars botApiEnabled and botNavPath. Safe to call repeatedly; a
		///        value set before registration is kept.
		void RegisterConsoleVars();

		/// @brief Whether the bot API is switched on (CVar botApiEnabled). False while the CVar
		///        is not registered.
		[[nodiscard]] bool IsEnabled();
	}

	/// @brief Lua functions that let a script play the real client: unit positions and ranges,
	///        nav mesh movement, facing and interaction. Dev builds only (MMO_WITH_CLIENT_BOT_API),
	///        and every function does nothing and returns nil while bot_api::IsEnabled() is false.
	/// @details Movement synthesizes player input (facing plus the move-forward key), so the bot
	///          drives the same prediction, collision and packet path a player does.
	class BotApi final : public NonCopyable
	{
	public:
		/// @brief How the most recent move request stands, as reported to Lua.
		enum class MoveStatus
		{
			Idle,
			Moving,
			Arrived,
			NoPath,
			NoNavmesh,
			Stuck
		};

	public:
		BotApi(RealmConnector& realmConnector, const proto_client::Project& project);

	public:
		/// @brief Registers the bot functions as Lua globals.
		void RegisterLuaFunctions(lua_State* luaState);

		/// @brief Drives an active move. Call every frame before the player controller updates,
		///        so the input set here is applied in the same frame.
		void Update(GameTime now);

		/// @brief Stops any move, for leaving the world. The nav data stays loaded.
		void Reset();

	private:
		enum class MoveMode
		{
			None,
			Point,
			Target
		};

		bool MoveTo(const Vector3& goal);

		bool MoveToTarget(float range);

		/// @brief Paths from the unit to goal and starts following the path.
		bool StartPath(GameUnitC& unit, const Vector3& goal, GameTime now);

		/// @brief Advances along the path and turns the outcome into input or a final status.
		void FollowPath(PlayerController& controller, GameUnitC& unit, GameTime now, bool sendFacingNow);

		/// @brief Turns the unit towards a point, sending the facing at most as often as mouse
		///        steering does.
		void FacePoint(GameUnitC& unit, const Vector3& point, GameTime now, bool sendNow);

		void Stop(MoveStatus status);

		void ReleaseMoveKey();

		[[nodiscard]] const char* GetMoveStatusName() const;

	private:
		RealmConnector& m_realmConnector;
		const proto_client::Project& m_project;
		BotNav m_nav;
		BotPathFollower m_follower;

		MoveMode m_mode { MoveMode::None };
		MoveStatus m_status { MoveStatus::Idle };

		/// The point MoveTo was asked to reach; the path may end short of it.
		Vector3 m_pointGoal {};

		/// MoveToTarget's range, compared against the 3D distance to the target.
		float m_targetRange { 0.0f };
		GameTime m_nextRepath { 0 };

		/// Whether this API set the move-forward key, so a developer's own key press is never
		/// released by it.
		bool m_drivingKey { false };

		GameTime m_nextFacingSend { 0 };
		bool m_facingSendPending { false };
	};
}
