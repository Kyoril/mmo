// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_actions.h"

#include "log/default_log_levels.h"

#include <chrono>

using namespace std::chrono_literals;

namespace mmo
{
	/// A simple bot profile that sends a greeting message and then idles.
	/// This replicates the original bot behavior.
	class SimpleGreeterProfile final : public BotProfile
	{
	public:
		explicit SimpleGreeterProfile(std::string greetingMessage)
			: m_greetingMessage(std::move(greetingMessage))
		{
		}

		std::string GetName() const override
		{
			return "SimpleGreeter";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("SimpleGreeter profile activated");

			// Queue greeting message if configured
			if (!m_greetingMessage.empty())
			{
				QueueAction(std::make_shared<ChatMessageAction>(m_greetingMessage, ChatType::Say));
			}

			// Queue periodic heartbeats
			QueueAction(std::make_shared<WaitAction>(5000ms));
			QueueAction(std::make_shared<HeartbeatAction>());
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// Queue more heartbeats
			QueueAction(std::make_shared<WaitAction>(5000ms));
			QueueAction(std::make_shared<HeartbeatAction>());
			return true;
		}

	private:
		std::string m_greetingMessage;
	};

	/// A bot profile that performs a sequence of chat messages with delays.
	/// Useful for testing chat functionality.
	class ChatterProfile final : public BotProfile
	{
	public:
		ChatterProfile(std::vector<std::string> messages, std::chrono::milliseconds delayBetweenMessages)
			: m_messages(std::move(messages))
			, m_delay(delayBetweenMessages)
		{
		}

		std::string GetName() const override
		{
			return "Chatter";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Chatter profile activated with " << m_messages.size() << " messages");

			// Queue all messages with delays
			for (size_t i = 0; i < m_messages.size(); ++i)
			{
				if (i > 0)
				{
					QueueAction(std::make_shared<WaitAction>(m_delay));
				}
				QueueAction(std::make_shared<ChatMessageAction>(m_messages[i], ChatType::Say));
			}

			// Add final heartbeat to keep alive
			QueueAction(std::make_shared<WaitAction>(5000ms));
			QueueAction(std::make_shared<HeartbeatAction>());
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// Keep sending heartbeats
			QueueAction(std::make_shared<WaitAction>(5000ms));
			QueueAction(std::make_shared<HeartbeatAction>());
			return true;
		}

	private:
		std::vector<std::string> m_messages;
		std::chrono::milliseconds m_delay;
	};

	/// A bot profile that moves to a series of waypoints.
	/// Useful for testing movement and pathfinding.
	class PatrolProfile final : public BotProfile
	{
	public:
		PatrolProfile(std::vector<Vector3> waypoints, bool loop = true)
			: m_waypoints(std::move(waypoints))
			, m_loop(loop)
		{
		}

		std::string GetName() const override
		{
			return "Patrol";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Patrol profile activated with " << m_waypoints.size() << " waypoints");

			// Queue movement to all waypoints
			for (const auto& waypoint : m_waypoints)
			{
				QueueAction(std::make_shared<MoveToPositionAction>(waypoint));
				QueueAction(std::make_shared<WaitAction>(1000ms)); // Pause at each waypoint
			}
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			if (m_loop)
			{
				// Restart patrol
				ILOG("Restarting patrol loop");
				for (const auto& waypoint : m_waypoints)
				{
					QueueAction(std::make_shared<MoveToPositionAction>(waypoint));
					QueueAction(std::make_shared<WaitAction>(1000ms));
				}
				return true;
			}

			// Done patrolling, just send heartbeats
			QueueAction(std::make_shared<WaitAction>(5000ms));
			QueueAction(std::make_shared<HeartbeatAction>());
			return true;
		}

	private:
		std::vector<Vector3> m_waypoints;
		bool m_loop;
	};

	/// A bot profile that combines multiple behaviors in sequence.
	/// This demonstrates how to build complex bot behaviors from simple actions.
	class SequenceProfile final : public BotProfile
	{
	public:
		std::string GetName() const override
		{
			return "Sequence";
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Sequence profile activated - demonstrating various actions");

			// 1. Greet
			QueueAction(std::make_shared<ChatMessageAction>("Hello everyone!", ChatType::Say));
			QueueAction(std::make_shared<WaitAction>(2000ms));

			// 2. Announce movement
			QueueAction(std::make_shared<ChatMessageAction>("I'm going to move around a bit...", ChatType::Say));
			QueueAction(std::make_shared<WaitAction>(1000ms));

			// 3. Move in a small square pattern (relative to spawn position)
			const MovementInfo& info = context.GetMovementInfo();
			const Vector3 startPos = info.position;
			
			QueueAction(std::make_shared<MoveToPositionAction>(startPos + Vector3(5, 0, 0)));
			QueueAction(std::make_shared<WaitAction>(500ms));
			
			QueueAction(std::make_shared<MoveToPositionAction>(startPos + Vector3(5, 5, 0)));
			QueueAction(std::make_shared<WaitAction>(500ms));
			
			QueueAction(std::make_shared<MoveToPositionAction>(startPos + Vector3(0, 5, 0)));
			QueueAction(std::make_shared<WaitAction>(500ms));
			
			QueueAction(std::make_shared<MoveToPositionAction>(startPos));
			QueueAction(std::make_shared<WaitAction>(1000ms));

			// 4. Announce completion
			QueueAction(std::make_shared<ChatMessageAction>("Done! That was fun!", ChatType::Say));
			QueueAction(std::make_shared<WaitAction>(2000ms));

			// 5. Farewell
			QueueAction(std::make_shared<ChatMessageAction>("Goodbye for now!", ChatType::Say));
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// After sequence completes, just maintain connection
			QueueAction(std::make_shared<WaitAction>(5000ms));
			QueueAction(std::make_shared<HeartbeatAction>());
			return true;
		}
	};
}
