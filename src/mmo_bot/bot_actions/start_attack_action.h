// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"
#include "bot_unit.h"

#include "log/default_log_levels.h"

namespace mmo
{
	/// @brief Action that starts auto-attack against a target.
	/// 
	/// This action sends the attack swing packet to the server to begin auto-attacking.
	/// It completes immediately after sending the packet - the actual combat is handled
	/// asynchronously by the server.
	class StartAttackAction final : public BotAction
	{
	public:
		/// @brief Constructs a start attack action.
		/// @param targetGuid The GUID of the target to attack.
		explicit StartAttackAction(uint64 targetGuid)
			: m_targetGuid(targetGuid)
		{
		}

		std::string GetDescription() const override
		{
			return "Start attacking target";
		}

		bool IsInterruptible() const override
		{
			return false;
		}

		ActionResult Execute(BotContext& context) override
		{
			if (m_targetGuid == 0)
			{
				WLOG("StartAttackAction: No target specified");
				return ActionResult::Failed;
			}

			// Check if target exists
			const BotUnit* target = context.GetUnit(m_targetGuid);
			if (!target)
			{
				WLOG("StartAttackAction: Target " << std::hex << m_targetGuid << std::dec << " not found");
				return ActionResult::Failed;
			}

			// Check if target is already dead
			if (target->IsDead())
			{
				WLOG("StartAttackAction: Target is dead");
				return ActionResult::Failed;
			}

			// Send attack start packet
			context.StartAutoAttack(m_targetGuid);
			DLOG("StartAttackAction: Started attacking target " << std::hex << m_targetGuid << std::dec);

			return ActionResult::Success;
		}

		void OnAbort(BotContext& context) override
		{
			// Nothing to abort
		}

	private:
		uint64 m_targetGuid;
	};

}
