// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_profile.h"
#include "bot_actions/chat_message_action.h"
#include "bot_actions/wait_action.h"
#include "bot_actions/accept_party_invitation_action.h"

#include "log/default_log_levels.h"

#include <chrono>
#include <string>

using namespace std::chrono_literals;

namespace mmo
{
	/// A simple bot profile that sends a greeting message and then idles.
	/// This replicates the original bot behavior.
	/// Note: The bot doesn't need to send periodic "keep-alive" packets - the server
	/// handles disconnection of idle clients through timeouts.
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

			// After greeting, just wait indefinitely (server will disconnect if needed)
			QueueAction(std::make_shared<WaitAction>(std::chrono::hours(24)));
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			// Queue more waiting (effectively idle)
			QueueAction(std::make_shared<WaitAction>(std::chrono::hours(24)));
			return true;
		}

		/// Example: Accept all party invitations with a 2-second delay to simulate human behavior.
		/// Override this method to customize party invitation handling.
		bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
		{
			ILOG("SimpleGreeter: Received party invitation from " << inviterName << " - accepting with delay");
			
			// Queue a wait action to simulate "thinking time"
			QueueActionNext(std::make_shared<WaitAction>(2000ms));
			
			// Then queue the accept action
			QueueActionNext(std::make_shared<AcceptPartyInvitationAction>());
			
			// Return true to indicate we want to accept (but not immediately)
			return true;
		}

	private:
		std::string m_greetingMessage;
	};
}
