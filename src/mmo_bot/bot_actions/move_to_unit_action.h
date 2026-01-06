// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"
#include "bot_unit.h"

#include "base/clock.h"
#include "log/default_log_levels.h"
#include "game/movement_info.h"
#include "game_protocol/game_protocol.h"

#include <chrono>
#include <sstream>
#include <cmath>

using namespace std::chrono_literals;

namespace mmo
{
	/// @brief Action that moves the bot towards a target unit until within a specified range.
	/// 
	/// This action handles:
	/// - Facing the target
	/// - Moving forward towards the target
	/// - Simulating position updates while moving
	/// - Stopping when in range
	/// - Timeout if the target is unreachable
	class MoveToUnitAction final : public BotAction
	{
	public:
		/// @brief Constructs a move-to-unit action.
		/// @param targetGuid The GUID of the unit to move towards.
		/// @param desiredRange The distance to stop at (default 3 yards for melee).
		/// @param timeout Maximum time to spend moving (default 30 seconds).
		explicit MoveToUnitAction(
			uint64 targetGuid,
			float desiredRange = 3.0f,
			std::chrono::milliseconds timeout = 30000ms)
			: m_targetGuid(targetGuid)
			, m_desiredRange(desiredRange)
			, m_timeout(timeout)
		{
		}

		std::string GetDescription() const override
		{
			std::ostringstream oss;
			oss << "Move to unit 0x" << std::hex << m_targetGuid 
				<< " (within " << std::dec << m_desiredRange << " yards)";
			return oss.str();
		}

		ActionResult Execute(BotContext& context) override
		{
			const GameTime currentTime = context.GetServerTime();

			// Initialize on first execution
			if (m_startTime == 0)
			{
				m_startTime = currentTime;
				m_lastUpdateTime = currentTime;
			}

			// Check timeout
			if (currentTime - m_startTime > static_cast<GameTime>(m_timeout.count()))
			{
				WLOG("[MOVE] Timeout reached while moving to target");
				StopIfMoving(context);
				return ActionResult::Failed;
			}

			// Get the target unit
			const BotUnit* target = context.GetUnit(m_targetGuid);
			if (!target)
			{
				WLOG("[MOVE] Target unit no longer exists");
				StopIfMoving(context);
				return ActionResult::Failed;
			}

			// Check if target died
			if (target->IsDead())
			{
				DLOG("[MOVE] Target is dead");
				StopIfMoving(context);
				return ActionResult::Failed;
			}

			// Get current distance
			float distance = context.GetDistanceToUnit(m_targetGuid);

			// Check if we're within range
			if (distance <= m_desiredRange)
			{
				ILOG("[MOVE] Reached target at " << distance << " yards");
				StopIfMoving(context);
				return ActionResult::Success;
			}

			// Calculate time delta since last update
			const GameTime timeDelta = currentTime - m_lastUpdateTime;

			// Update position simulation and send heartbeats periodically
			if (timeDelta >= m_updateIntervalMs)
			{
				m_lastUpdateTime = currentTime;

				// Get target position and calculate direction
				const Vector3 targetPos = target->GetPosition();
				
				// Get current movement info
				MovementInfo info = context.GetMovementInfo();
				const Vector3 currentPos = info.position;

				// Calculate direction to target
				Vector3 direction = targetPos - currentPos;
				direction.y = 0.0f; // Keep on ground plane
				const float dist = direction.GetLength();

				if (dist > 0.001f)
				{
					direction /= dist; // Normalize

					// Calculate new facing angle
					const Radian newFacing(std::atan2(direction.x, direction.z));
					info.facing = newFacing;

					// If not moving yet, start moving
					if (!m_isMoving)
					{
						DLOG("[MOVE] Starting movement towards target at " << distance << " yards");
						info.movementFlags |= movement_flags::Forward;
						info.timestamp = currentTime;
						context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, info);
						m_isMoving = true;
					}

					// Simulate movement: move towards target based on run speed
					// Run speed is typically 7.0 yards/second
					const float runSpeed = 7.0f;
					const float moveDistance = runSpeed * (static_cast<float>(timeDelta) / 1000.0f);
					
					// Don't overshoot the target
					const float actualMove = std::min(moveDistance, dist - m_desiredRange + 0.5f);
					
					if (actualMove > 0.0f)
					{
						info.position = currentPos + direction * actualMove;
						info.timestamp = currentTime;
						
						// Send heartbeat to sync position with server
						context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, info);
					}
				}
			}

			return ActionResult::InProgress;
		}

		void OnAbort(BotContext& context) override
		{
			StopIfMoving(context);
			m_startTime = 0;
			m_lastUpdateTime = 0;
		}

		/// Movement actions are interruptible.
		bool IsInterruptible() const override
		{
			return true;
		}

	private:
		void StopIfMoving(BotContext& context)
		{
			if (m_isMoving)
			{
				MovementInfo info = context.GetMovementInfo();
				info.movementFlags &= ~movement_flags::Forward;
				info.timestamp = context.GetServerTime();
				context.SendMovementUpdate(game::client_realm_packet::MoveStop, info);
				m_isMoving = false;
			}
		}

	private:
		uint64 m_targetGuid;
		float m_desiredRange;
		std::chrono::milliseconds m_timeout;

		static constexpr GameTime m_updateIntervalMs { 100 }; // Update every 100ms
		GameTime m_startTime { 0 };
		GameTime m_lastUpdateTime { 0 };
		bool m_isMoving { false };
	};

}
