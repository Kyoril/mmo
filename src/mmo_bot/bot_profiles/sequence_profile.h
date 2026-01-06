// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_actions/chat_message_action.h"
#include "bot_actions/move_to_position_action.h"
#include "bot_actions/wait_action.h"

#include "log/default_log_levels.h"

#include <chrono>

using namespace std::chrono_literals;

namespace mmo
{
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
			
			QueueAction(std::make_shared<MoveToPositionAction>(startPos + Vector3(5, 0, 5)));
			QueueAction(std::make_shared<WaitAction>(500ms));
			
			QueueAction(std::make_shared<MoveToPositionAction>(startPos + Vector3(0, 0, 5)));
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
			// After sequence completes, just idle
			QueueAction(std::make_shared<WaitAction>(std::chrono::hours(1)));
			return true;
		}
	};
}
