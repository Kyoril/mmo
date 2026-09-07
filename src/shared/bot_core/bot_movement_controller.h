// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_movement_math.h"

#include "base/signal.h"
#include "base/typedefs.h"
#include "game/movement_info.h"
#include "math/vector3.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mmo
{
	class BotContext;

	enum class BotMovementStatus : uint8
	{
		Idle = 0,
		Moving = 1,
		Reached = 2,
		Unreachable = 3,
		Stuck = 4,
	};

	struct BotMovementSettings final
	{
		float acceptanceRadius { 1.0f };
		float waypointAcceptanceRadius { 0.75f };
		float turnSmoothingThresholdRadians { 0.524f };
		float turnSmoothingDistance { 0.5f };
		float maxAcceleration { 40.48f };
		float fallbackRunSpeed { 7.0f };
		GameTime heartbeatIntervalMs { 500 };
		float progressDistanceEpsilon { 0.15f };
		GameTime nonProgressTimeoutMs { 3000 };
	};

	struct BotMovementEvent final
	{
		BotMovementStatus status { BotMovementStatus::Idle };
		std::string reason;
		uint32 mapId { 0 };
		Vector3 target { Vector3::Zero };
		Vector3 position { Vector3::Zero };
		std::size_t waypointIndex { 0 };
		std::size_t pathPointCount { 0 };
	};

	class BotMovementController final
	{
	public:
		signal<void(const BotMovementEvent&)> MovementStarted;
		signal<void(const BotMovementEvent&)> WaypointAdvanced;
		signal<void(const BotMovementEvent&)> TargetReached;
		signal<void(const BotMovementEvent&)> TargetUnreachable;
		signal<void(const BotMovementEvent&)> MovementStopped;

	public:
		explicit BotMovementController(BotMovementSettings settings = {});

		[[nodiscard]] BotMovementStatus GetStatus() const noexcept { return m_status; }
		[[nodiscard]] bool IsActive() const noexcept { return m_status == BotMovementStatus::Moving; }
		[[nodiscard]] const std::string& GetLastReason() const noexcept { return m_lastReason; }
		[[nodiscard]] const std::vector<Vector3>& GetPath() const noexcept { return m_path; }

		/// Where the current path is headed. Only meaningful while the controller is active;
		/// callers use it to decide whether a new destination is worth re-pathing for.
		[[nodiscard]] const Vector3& GetTarget() const noexcept { return m_target; }

		bool MoveTo(BotContext& context, const Vector3& target, float acceptanceRadius = -1.0f);
		BotMovementStatus Update(BotContext& context);
		void Stop(BotContext& context, std::string reason = "stopped");
		void Reset();

	private:
		[[nodiscard]] bool PlanPath(BotContext& context, const Vector3& target, float acceptanceRadius);
		[[nodiscard]] float ResolveRunSpeed(const BotContext& context) const;
		[[nodiscard]] BotMovementEvent BuildEvent(BotContext& context, BotMovementStatus status, std::string reason) const;
		void EmitUnreachable(BotContext& context, std::string reason);
		void EmitReached(BotContext& context, std::string reason);
		void StopLowLevel(BotContext& context, std::string reason, bool emitSignal);
		void SetStatus(BotMovementStatus status, std::string reason);

	private:
		BotMovementSettings m_settings;
		BotMovementRuntimeState m_runtime;
		std::vector<Vector3> m_path;
		Vector3 m_target { Vector3::Zero };
		uint32 m_mapId { 0 };
		std::size_t m_nextWaypointIndex { 0 };
		BotMovementStatus m_status { BotMovementStatus::Idle };
		std::string m_lastReason;
	};
}
