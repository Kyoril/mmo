// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"

#include "base/clock.h"
#include "game/chat_type.h"
#include "game_protocol/game_protocol.h"

#include <chrono>
#include <sstream>

namespace mmo
{
	/// Action that sends a chat message.
	class ChatMessageAction final : public BotAction
	{
	public:
		explicit ChatMessageAction(
			std::string message,
			ChatType chatType = ChatType::Say,
			std::string target = "")
			: m_message(std::move(message))
			, m_chatType(chatType)
			, m_target(std::move(target))
		{
		}

		std::string GetDescription() const override
		{
			return "Send chat message: \"" + m_message + "\"";
		}

		ActionResult Execute(BotContext& context) override
		{
			if (!context.IsWorldReady())
			{
				return ActionResult::Failed;
			}

			context.SendChatMessage(m_message, m_chatType, m_target);
			return ActionResult::Success;
		}

		bool CanExecute(const BotContext& context, std::string& outReason) const override
		{
			if (!context.IsWorldReady())
			{
				outReason = "Bot is not in the world yet";
				return false;
			}

			return true;
		}

	private:
		std::string m_message;
		ChatType m_chatType;
		std::string m_target;
	};

	/// Action that waits for a specified duration.
	class WaitAction final : public BotAction
	{
	public:
		explicit WaitAction(std::chrono::milliseconds duration)
			: m_duration(duration)
			, m_startTime(0)
		{
		}

		std::string GetDescription() const override
		{
			const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(m_duration).count();
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(m_duration).count() % 1000;
			
			std::ostringstream oss;
			oss << "Wait for ";
			if (seconds > 0)
			{
				oss << seconds << "s";
			}
			if (ms > 0)
			{
				if (seconds > 0)
				{
					oss << " ";
				}
				oss << ms << "ms";
			}
			return oss.str();
		}

		ActionResult Execute(BotContext& context) override
		{
			// Initialize start time on first execution
			if (m_startTime == 0)
			{
				m_startTime = context.GetServerTime();
				return ActionResult::InProgress;
			}

			// Check if enough time has passed
			const GameTime currentTime = context.GetServerTime();
			const auto elapsed = std::chrono::milliseconds(currentTime - m_startTime);
			
			if (elapsed >= m_duration)
			{
				return ActionResult::Success;
			}

			return ActionResult::InProgress;
		}

		void OnAbort(BotContext& context) override
		{
			m_startTime = 0;
		}

	private:
		std::chrono::milliseconds m_duration;
		GameTime m_startTime;
	};

	/// Action that moves the bot to a target position.
	/// Note: This is a basic implementation. A more sophisticated version would:
	/// - Use pathfinding to navigate around obstacles
	/// - Handle collision detection
	/// - Support different movement speeds
	class MoveToPositionAction final : public BotAction
	{
	public:
		explicit MoveToPositionAction(Vector3 targetPosition, float acceptanceRadius = 1.0f)
			: m_targetPosition(targetPosition)
			, m_acceptanceRadius(acceptanceRadius)
			, m_isMoving(false)
		{
		}

		std::string GetDescription() const override
		{
			std::ostringstream oss;
			oss << "Move to position ("
				<< m_targetPosition.x << ", "
				<< m_targetPosition.y << ", "
				<< m_targetPosition.z << ")";
			return oss.str();
		}

		ActionResult Execute(BotContext& context) override
		{
			if (!context.IsWorldReady())
			{
				return ActionResult::Failed;
			}

			MovementInfo currentMovement = context.GetMovementInfo();
			const Vector3 currentPosition = currentMovement.position;

			// Calculate distance to target
			const Vector3 delta = m_targetPosition - currentPosition;
			const float distance = delta.GetLength();

			// Check if we've reached the target
			if (distance <= m_acceptanceRadius)
			{
				// Stop movement if we were moving
				if (m_isMoving)
				{
					currentMovement.movementFlags &= ~movement_flags::Forward;
					currentMovement.timestamp = context.GetServerTime();
					context.SendMovementUpdate(game::client_realm_packet::MoveStop, currentMovement);
					context.UpdateMovementInfo(currentMovement);
					m_isMoving = false;
				}

				return ActionResult::Success;
			}

			// Calculate facing direction
			const float targetFacing = std::atan2(delta.y, delta.x);
			currentMovement.facing = Radian(targetFacing);
			
			// Start moving forward if not already moving
			if (!m_isMoving)
			{
				currentMovement.movementFlags |= movement_flags::Forward;
				currentMovement.timestamp = context.GetServerTime();
				context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, currentMovement);
				context.UpdateMovementInfo(currentMovement);
				m_isMoving = true;
			}
			else
			{
				// Update position and send heartbeat
				// Note: In a real implementation, we'd calculate the new position based on
				// movement speed and elapsed time. For now, we'll just update the facing.
				currentMovement.timestamp = context.GetServerTime();
				context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, currentMovement);
				context.UpdateMovementInfo(currentMovement);
			}

			return ActionResult::InProgress;
		}

		void OnAbort(BotContext& context) override
		{
			// Stop movement if we were moving
			if (m_isMoving)
			{
				MovementInfo currentMovement = context.GetMovementInfo();
				currentMovement.movementFlags &= ~movement_flags::Forward;
				currentMovement.timestamp = context.GetServerTime();
				context.SendMovementUpdate(game::client_realm_packet::MoveStop, currentMovement);
				context.UpdateMovementInfo(currentMovement);
				m_isMoving = false;
			}
		}

		bool CanExecute(const BotContext& context, std::string& outReason) const override
		{
			if (!context.IsWorldReady())
			{
				outReason = "Bot is not in the world yet";
				return false;
			}

			return true;
		}

	private:
		Vector3 m_targetPosition;
		float m_acceptanceRadius;
		bool m_isMoving;
	};

	/// Action that sends a heartbeat (movement update without changing position).
	/// Useful for keeping the connection alive.
	class HeartbeatAction final : public BotAction
	{
	public:
		std::string GetDescription() const override
		{
			return "Send heartbeat";
		}

		ActionResult Execute(BotContext& context) override
		{
			if (!context.IsWorldReady())
			{
				return ActionResult::Failed;
			}

			const uint64 guid = context.GetSelectedCharacterGuid();
			if (guid == 0)
			{
				return ActionResult::Failed;
			}

			MovementInfo info = context.GetMovementInfo();
			info.timestamp = context.GetServerTime();
			context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, info);

			return ActionResult::Success;
		}

		bool CanExecute(const BotContext& context, std::string& outReason) const override
		{
			if (!context.IsWorldReady())
			{
				outReason = "Bot is not in the world yet";
				return false;
			}

			if (context.GetSelectedCharacterGuid() == 0)
			{
				outReason = "No character selected";
				return false;
			}

			return true;
		}
	};
}
