// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"

namespace mmo
{
	/// Action that accepts a pending party invitation.
	/// This action should typically be queued by a bot profile in response to
	/// an OnPartyInvitation event, allowing for delayed acceptance to simulate
	/// more realistic human behavior.
	class AcceptPartyInvitationAction final : public BotAction
	{
	public:
		explicit AcceptPartyInvitationAction() = default;

		std::string GetDescription() const override
		{
			return "Accept party invitation";
		}

		ActionResult Execute(BotContext& context) override
		{
			if (!context.IsWorldReady())
			{
				return ActionResult::Failed;
			}

			context.AcceptPartyInvitation();
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
	};
}
