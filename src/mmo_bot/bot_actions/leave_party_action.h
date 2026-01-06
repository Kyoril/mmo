// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "log/default_log_levels.h"

namespace mmo
{
	/// @brief Action that makes the bot leave its current party.
	class LeavePartyAction final : public IBotAction
	{
	public:
		/// @brief Creates a new LeavePartyAction.
		LeavePartyAction() = default;

		/// @copydoc IBotAction::GetName
		[[nodiscard]] std::string_view GetName() const override
		{
			return "LeaveParty";
		}

		/// @copydoc IBotAction::Start
		void Start(BotContext& context) override
		{
			if (!context.IsInParty())
			{
				WLOG("LeavePartyAction: Bot is not in a party");
				return;
			}

			DLOG("Leaving party...");
			context.LeaveParty();
		}

		/// @copydoc IBotAction::Update
		[[nodiscard]] ActionResult Update(BotContext& context, float deltaSeconds) override
		{
			// This is an instant action - completes immediately after sending the leave request
			return ActionResult::Completed;
		}
	};
}
