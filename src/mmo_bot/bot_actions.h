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
	/// Movement protocol:
	/// 1. Send MoveStartForward with Forward flag added
	/// 2. Send periodic MoveHeartBeat packets (every ~500ms) with updated position while moving
	/// 3. Send MoveStop with Forward flag removed when destination reached
	/// 
	/// Note: This is a basic implementation. A more sophisticated version would:
	/// - Use pathfinding to navigate around obstacles
	/// - Handle collision detection
	/// - Calculate actual position based on movement speed and elapsed time
	/// - Support different movement types (backward, strafe, etc.)
	class MoveToPositionAction final : public BotAction
	{
	public:
		explicit MoveToPositionAction(Vector3 targetPosition, float acceptanceRadius = 1.0f, float moveSpeed = 7.0f)
			: m_targetPosition(targetPosition)
			, m_acceptanceRadius(acceptanceRadius)
			, m_moveSpeed(moveSpeed)
			, m_isMoving(false)
			, m_lastHeartbeat(0)
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
			
			// Check if we have the FALLING flag from spawn - clear it before movement
			if (!m_isMoving && (currentMovement.movementFlags & movement_flags::Falling))
			{
				context.SendLandedPacket();
				currentMovement = context.GetMovementInfo(); // Get updated movement info
			}

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
					// Send MoveStop packet (removes Forward flag)
					currentMovement.movementFlags &= ~movement_flags::Forward;
					currentMovement.timestamp = context.GetServerTime();
					context.SendMovementUpdate(game::client_realm_packet::MoveStop, currentMovement);
					context.UpdateMovementInfo(currentMovement);
					m_isMoving = false;
				}

				return ActionResult::Success;
			}

			// Calculate facing direction (Y is height, so we use X and Z for horizontal direction)
			// Using atan2(-dz, dx) to match the game's coordinate system and GetAngle implementation
			const float targetFacing = std::atan2(-delta.z, delta.x);
			currentMovement.facing = Radian(targetFacing);
			
			// Start moving forward if not already moving
			if (!m_isMoving)
			{
				// Send MoveStartForward packet (adds Forward flag)
				currentMovement.movementFlags |= movement_flags::Forward;
				currentMovement.timestamp = context.GetServerTime();
				context.SendMovementUpdate(game::client_realm_packet::MoveStartForward, currentMovement);
				context.UpdateMovementInfo(currentMovement);
				m_isMoving = true;
				m_lastHeartbeat = context.GetServerTime();
			}
			else
			{
				// Send periodic heartbeat while moving (every 500ms)
				const GameTime now = context.GetServerTime();
				if (now - m_lastHeartbeat >= 500)
				{
					// Only calculate new position if movement flags indicate position CAN change
					// Server validates that position only changes when flags like Forward/Backward/Falling are set
					if (currentMovement.IsChangingPosition())
					{
						const float elapsed = (now - m_lastHeartbeat) / 1000.0f; // Convert to seconds
						const Vector3 direction = delta.NormalizedCopy();
						const float stepDistance = std::min(m_moveSpeed * elapsed, distance);
						currentMovement.position = currentPosition + (direction * stepDistance);
					}
					// else: keep position unchanged if we're not moving/falling
					
					// Send heartbeat packet
					// IMPORTANT: Do NOT modify movement flags during heartbeat!
					currentMovement.timestamp = now;
					context.SendMovementUpdate(game::client_realm_packet::MoveHeartBeat, currentMovement);
					context.UpdateMovementInfo(currentMovement); // Update local cache for next iteration
					m_lastHeartbeat = now;
				}
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
		float m_moveSpeed;
		bool m_isMoving;
		GameTime m_lastHeartbeat;
	};
}
