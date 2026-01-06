// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"

#include "log/default_log_levels.h"

namespace mmo
{
	/// @brief Action that stops auto-attack.
	/// 
	/// This action sends the attack stop packet to the server to cease auto-attacking.
	/// It completes immediately after sending the packet.
	class StopAttackAction final : public BotAction
	{
	public:
		/// @brief Default constructor.
		StopAttackAction() = default;

		std::string GetDescription() const override
		{
			return "Stop attacking";
		}

		bool IsInterruptible() const override
		{
			return false;
		}

		ActionResult Execute(BotContext& context) override
		{
			// Send attack stop packet
			context.StopAutoAttack();
			DLOG("StopAttackAction: Stopped attacking");

			return ActionResult::Success;
		}

		void OnAbort(BotContext& context) override
		{
			// Nothing to abort
		}
	};

}
