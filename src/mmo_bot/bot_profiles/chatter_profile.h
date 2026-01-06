// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_actions/chat_message_action.h"
#include "bot_actions/wait_action.h"

#include "log/default_log_levels.h"

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace mmo
{
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

			// After sending all messages, just idle
			QueueAction(std::make_shared<WaitAction>(std::chrono::hours(1)));
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// Keep sending messages or just idle
			QueueAction(std::make_shared<WaitAction>(std::chrono::hours(1)));
			return true;
		}

	private:
		std::vector<std::string> m_messages;
		std::chrono::milliseconds m_delay;
	};
}
